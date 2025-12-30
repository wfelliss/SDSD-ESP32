let runsList = [];

async function loadRuns() {
    try {
        const response = await fetch('/runs');
        const runs = await response.json();
        runsList = runs || [];
        renderRuns();
    } catch (error) {
        console.error("Failed to load runs:", error);
        document.getElementById('runsContainer').innerHTML = "<p>Error loading runs.</p>";
    }
}

function renderRuns() {
    const container = document.getElementById('runsContainer');
    if (!runsList || runsList.length === 0) {
        container.innerHTML = "<p>No runs recorded yet.</p>";
        return;
    }

    let html = "<ul>";
    runsList.forEach((run, index) => {
          html += `<li id="run-${index}">
                          <span>${run.name} (${run.size} bytes)</span>
                          <button onclick="addMetadata(${index})">Add metadata</button>
                          <button class="delete-btn" onclick="deleteRun(${index})">Delete</button>
                      </li>`;
    });
    html += "</ul>";
    container.innerHTML = html;
}

function addMetadata(index) {
    const li = document.getElementById(`run-${index}`);
    if (!li || !runsList[index]) return;
    const run = runsList[index];
    li.innerHTML = `
        <span>${run.name} (${run.size} bytes)</span>
        <div class="metadata-form">
            <label for="trackInput-${index}">Track name:</label>
            <input type="text" id="trackInput-${index}" placeholder="Enter track name" style="margin-top:4px;" />
            <label for="commentsInput-${index}">Comments:</label>
            <input type="text" id="commentsInput-${index}" placeholder="Enter comments" style="margin-top:4px;" />
            <label for="frontStrokeInput-${index}">Front Stroke:</label>
            <input type="text" id="frontStrokeInput-${index}" placeholder="Enter front stroke" style="margin-top:4px;" />
            <label for="rearStrokeInput-${index}">Rear Stroke:</label>
            <input type="text" id="rearStrokeInput-${index}" placeholder="Enter rear stroke" style="margin-top:4px;" />
            <div style="margin-top:6px;">
                <button onclick="uploadRun(${index})">Upload</button>
                <button onclick="renderRuns()">Cancel</button>
            </div>
        </div>
    `;
}
async function uploadRun(index) {
    const statusEl = document.getElementById('uploadStatus');
    if (!runsList || !runsList[index]) {
        statusEl.textContent = "Invalid run selected";
        return;
    }
    const runName = runsList[index].name;
    const trackInput = document.getElementById(`trackInput-${index}`);
    const commentsInput = document.getElementById(`commentsInput-${index}`);
    const trackName = trackInput && trackInput.value ? trackInput.value.trim() : '';
    const comments = commentsInput && commentsInput.value ? commentsInput.value.trim() : '';
    const frontStrokeInput = document.getElementById(`frontStrokeInput-${index}`);
    const rearStrokeInput = document.getElementById(`rearStrokeInput-${index}`);
    const frontStroke = frontStrokeInput && frontStrokeInput.value ? parseInt(frontStrokeInput.value.trim()) : 0;
    const rearStroke = rearStrokeInput && rearStrokeInput.value ? parseInt(rearStrokeInput.value.trim()) : 0;
    statusEl.textContent = `Uploading ${runName}${trackName ? ' (track: ' + trackName + ')' : ''}...`;

    try {
        const params = new URLSearchParams();
        params.append('run', runName);
        if (trackName) params.append('track', trackName);
        if (comments) params.append('comments', comments);
        if (frontStroke) params.append('front_stroke', frontStroke);
        if (rearStroke) params.append('rear_stroke', rearStroke);
        const response = await fetch('/uploadRun', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: params.toString()
        });
        if (response.ok) {
            statusEl.textContent = "Upload started";
            // Wait for server-side deletion of the file before reloading list
            const deleted = await waitForFileDeletion(runName, 60000, 1000);
            if (deleted) statusEl.textContent = "Upload finished — run removed";
            else statusEl.textContent = "Upload started (file still present)";
        } else {
            statusEl.textContent = "Upload failed: " + response.status;
        }
    } catch (err) {
        statusEl.textContent = "Upload failed: " + err.message;
    }
    loadRuns();
}
function deleteRun(index) {
    if (!runsList || !runsList[index]) return;
    const runName = runsList[index].name;
    const statusEl = document.getElementById('uploadStatus');
    fetch('/deleteRun', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ run: runName })
    }).then(response => {
        if (response.ok) {
            statusEl.textContent = "Run deleted: waiting for update...";
            // wait a short time for deletion to complete on server, then reload when gone
            waitForFileDeletion(runName, 20000, 800).then(deleted => {
                if (deleted) statusEl.textContent = "Run deleted: " + runName;
                else statusEl.textContent = "Run deletion may still be pending";
                loadRuns();
            });
        } else {
            statusEl.textContent = "Failed to delete run: " + response.status;
        }
    }).catch(err => {
        statusEl.textContent = "Error deleting run: " + err.message;
    });
}

// Poll /runs until the named run is no longer present or timeout elapses
async function waitForFileDeletion(runName, timeoutMs = 20000, intervalMs = 1000) {
    const start = Date.now();
    while ((Date.now() - start) < timeoutMs) {
        try {
            const resp = await fetch('/runs');
            if (!resp.ok) return false;
            const runs = await resp.json();
            const found = (runs || []).some(r => r.name === runName);
            if (!found) return true;
        } catch (e) {
            // ignore and retry until timeout
        }
        await new Promise(r => setTimeout(r, intervalMs));
    }
    return false;
}
// Load runs on page load
loadRuns();

// Add Refresh button functionality
document.getElementById('refreshBtn').addEventListener('click', loadRuns);
