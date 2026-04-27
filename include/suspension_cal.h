#pragma once

struct CalPoint {
    int raw;
    int corrected;
};

// Rear suspension — 77mm travel, correction applied to post-inversion value (4095 - raw)
static constexpr CalPoint REAR_SUS_CAL[] = {
    {0,    4095}, {95,   3828}, {578,  3559}, {935,  3298},
    {1254, 3023}, {1515, 2761}, {1773, 2494}, {2050, 2210},
    {2290, 1957}, {2575, 1675}, {2817, 1426}, {3050, 1167},
    {3291, 901},  {3556, 605},  {3773, 373},  {4014, 106},
    {4095, 0}
};

// Front suspension — 222.4mm travel, correction applied to direct read
static constexpr CalPoint FRONT_SUS_CAL[] = {
    {0,    4095}, {59,   3911}, {232,  3725}, {397,  3538},
    {564,  3354}, {724,  3173}, {886,  2989}, {1052, 2805},
    {1222, 2620}, {1396, 2433}, {1573, 2248}, {1746, 2063},
    {1934, 1864}, {2093, 1693}, {2263, 1507}, {2428, 1322},
    {2618, 1137}, {2795, 952},  {2980, 767},  {3195, 582},
    {3420, 396},  {3700, 211},  {4020, 26},   {4095, 0}
};

static constexpr int REAR_SUS_CAL_SIZE  = sizeof(REAR_SUS_CAL)  / sizeof(REAR_SUS_CAL[0]);
static constexpr int FRONT_SUS_CAL_SIZE = sizeof(FRONT_SUS_CAL) / sizeof(FRONT_SUS_CAL[0]);

inline int correctSuspension(int raw, const CalPoint* table, int tableSize) {
    if (raw <= table[0].raw)             return table[0].corrected;
    if (raw >= table[tableSize - 1].raw) return table[tableSize - 1].corrected;

    for (int i = 0; i < tableSize - 1; i++) {
        if (raw >= table[i].raw && raw <= table[i + 1].raw) {
            float t = (float)(raw - table[i].raw) / (table[i + 1].raw - table[i].raw);
            return (int)(table[i].corrected + t * (table[i + 1].corrected - table[i].corrected));
        }
    }

    return raw;
}
