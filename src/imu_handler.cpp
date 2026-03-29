#include "imu_handler.h"
#include <Wire.h>

static constexpr int   CAL_SAMPLES         = 50;
static constexpr int   CAL_SAMPLE_DELAY_MS = 10;
static constexpr float GRAVITY_FAULT_THRESHOLD = 0.5f;
static constexpr float GRAVITY_FALLBACK    = 9.806f;

void initImu(ImuState& imu) {
    Wire.begin();
    Wire.setClock(400000);  // 400 kHz fast mode — must be after Wire.begin()

    imu.ok = imu.device.begin_I2C();

    if (!imu.ok) {
        Serial.println("[IMU] not found — will record with zero IMU values");
        return;
    }

    imu.device.setAccelDataRate(LSM6DS_RATE_104_HZ);
    imu.device.setAccelRange(LSM6DS_ACCEL_RANGE_4_G);
    imu.device.setGyroDataRate(LSM6DS_RATE_104_HZ);
    imu.device.setGyroRange(LSM6DS_GYRO_RANGE_2000_DPS);
}

static void collectCalibrationSamples(ImuState& imu,
                                       double& sumGX, double& sumGY, double& sumGZ,
                                       double& sumAX, double& sumAY, double& sumAZ) {
    sensors_event_t accel, gyro, temp;

    for (int i = 0; i < CAL_SAMPLES; i++) {
        imu.device.getEvent(&accel, &gyro, &temp);

        sumGX += gyro.gyro.x;
        sumGY += gyro.gyro.y;
        sumGZ += gyro.gyro.z;
        sumAX += accel.acceleration.x;
        sumAY += accel.acceleration.y;
        sumAZ += accel.acceleration.z;

        vTaskDelay(pdMS_TO_TICKS(CAL_SAMPLE_DELAY_MS));
    }
}

static void computeBiasesFromSums(ImuState& imu,
                                   double sumGX, double sumGY, double sumGZ,
                                   double sumAX, double sumAY, double sumAZ) {
    imu.gyroBias[0]  = (float)(sumGX / CAL_SAMPLES);
    imu.gyroBias[1]  = (float)(sumGY / CAL_SAMPLES);
    imu.gyroBias[2]  = (float)(sumGZ / CAL_SAMPLES);
    imu.accelBias[0] = (float)(sumAX / CAL_SAMPLES);
    imu.accelBias[1] = (float)(sumAY / CAL_SAMPLES);
    imu.accelBias[2] = (float)(sumAZ / CAL_SAMPLES);

    Serial.printf("[CAL] gyro  bias x=%.4f y=%.4f z=%.4f rad/s\n",
                  imu.gyroBias[0], imu.gyroBias[1], imu.gyroBias[2]);
    Serial.printf("[CAL] accel bias x=%.4f y=%.4f z=%.4f m/s2\n",
                  imu.accelBias[0], imu.accelBias[1], imu.accelBias[2]);
}

static void resetRotationMatrixToIdentity(ImuState& imu) {
    float badGravMag = imu.gravMag;
    imu.gravMag   = GRAVITY_FALLBACK;
    imu.R[0][0]=1; imu.R[0][1]=0; imu.R[0][2]=0;
    imu.R[1][0]=0; imu.R[1][1]=1; imu.R[1][2]=0;
    imu.R[2][0]=0; imu.R[2][1]=0; imu.R[2][2]=1;
    Serial.printf("[CAL] ERROR gravMag=%.3f — sensor fault? R reset to identity\n", badGravMag);
}

static void buildRotationMatrix(ImuState& imu) {
    float ax = imu.accelBias[0], ay = imu.accelBias[1], az = imu.accelBias[2];
    imu.gravMag = sqrtf(ax*ax + ay*ay + az*az);

    if (imu.gravMag < GRAVITY_FAULT_THRESHOLD) {
        resetRotationMatrixToIdentity(imu);
        return;
    }

    // World frame: Z = down (gravity direction), X/Y are arbitrary horizontal axes.
    float wz[3] = { ax/imu.gravMag, ay/imu.gravMag, az/imu.gravMag };

    // Pick an arbitrary vector not parallel to wz for Gram-Schmidt orthogonalisation
    float tx = 1.0f, ty = 0.0f;
    float d = tx*wz[0] + ty*wz[1];
    if (fabsf(d) > 0.9f) { tx = 0.0f; ty = 1.0f; d = wz[1]; }

    // wx = normalise(tmp - (tmp·wz)*wz)
    float wx[3] = { tx - d*wz[0], ty - d*wz[1], -d*wz[2] };
    float s = sqrtf(wx[0]*wx[0] + wx[1]*wx[1] + wx[2]*wx[2]);
    wx[0]/=s; wx[1]/=s; wx[2]/=s;

    // wy = wz × wx  (right-handed, already unit length)
    float wy[3] = {
        wz[1]*wx[2] - wz[2]*wx[1],
        wz[2]*wx[0] - wz[0]*wx[2],
        wz[0]*wx[1] - wz[1]*wx[0]
    };

    imu.R[0][0]=wx[0]; imu.R[0][1]=wx[1]; imu.R[0][2]=wx[2];
    imu.R[1][0]=wy[0]; imu.R[1][1]=wy[1]; imu.R[1][2]=wy[2];
    imu.R[2][0]=wz[0]; imu.R[2][1]=wz[1]; imu.R[2][2]=wz[2];

    Serial.printf("[CAL] gravMag=%.3f R=[[%.3f,%.3f,%.3f],[%.3f,%.3f,%.3f],[%.3f,%.3f,%.3f]]\n",
                  imu.gravMag,
                  imu.R[0][0], imu.R[0][1], imu.R[0][2],
                  imu.R[1][0], imu.R[1][1], imu.R[1][2],
                  imu.R[2][0], imu.R[2][1], imu.R[2][2]);
}

void calibrateImu(ImuState& imu) {
    if (!imu.ok) {
        Serial.println("[CAL] IMU not found — recording with zero gyro bias");
        return;
    }

    double sumGX = 0, sumGY = 0, sumGZ = 0;
    double sumAX = 0, sumAY = 0, sumAZ = 0;

    collectCalibrationSamples(imu, sumGX, sumGY, sumGZ, sumAX, sumAY, sumAZ);
    computeBiasesFromSums(imu, sumGX, sumGY, sumGZ, sumAX, sumAY, sumAZ);
    buildRotationMatrix(imu);
}

void populateImuReadingIntoLine(ImuState& imu, SensorLine& line) {
    if (!imu.ok) {
        memset(line.acc, 0, sizeof(line.acc));
        return;
    }

    sensors_event_t accel, gyro, temp;
    bool readOk = imu.device.getEvent(&accel, &gyro, &temp);

    if (!readOk) {
        memset(line.acc, 0, sizeof(line.acc));
        return;
    }

    float gx = gyro.gyro.x - imu.gyroBias[0];
    float gy = gyro.gyro.y - imu.gyroBias[1];
    float gz = gyro.gyro.z - imu.gyroBias[2];
    float ax = accel.acceleration.x;
    float ay = accel.acceleration.y;
    float az = accel.acceleration.z;

    // Gyro → world frame, milli-rad/s
    line.acc[0] = (int)((imu.R[0][0]*gx + imu.R[0][1]*gy + imu.R[0][2]*gz) * 1000.0f);
    line.acc[1] = (int)((imu.R[1][0]*gx + imu.R[1][1]*gy + imu.R[1][2]*gz) * 1000.0f);
    line.acc[2] = (int)((imu.R[2][0]*gx + imu.R[2][1]*gy + imu.R[2][2]*gz) * 1000.0f);

    // Accel → world frame, gravity removed from Z, milli-g
    line.acc[3] = (int)((imu.R[0][0]*ax + imu.R[0][1]*ay + imu.R[0][2]*az) / imu.gravMag * 1000.0f);
    line.acc[4] = (int)((imu.R[1][0]*ax + imu.R[1][1]*ay + imu.R[1][2]*az) / imu.gravMag * 1000.0f);
    line.acc[5] = (int)((imu.R[2][0]*ax + imu.R[2][1]*ay + imu.R[2][2]*az - imu.gravMag) / imu.gravMag * 1000.0f);
}
