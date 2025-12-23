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
    statusEl.textContent = `Uploading ${runName}${trackName ? ' (track: ' + trackName + ')' : ''}...`;

    try {
        const params = new URLSearchParams();
        params.append('run', runName);
        if (trackName) params.append('track', trackName);
        if (comments) params.append('comments', comments);
        const response = await fetch('/uploadRun', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: params.toString()
        });
        if (response.ok) statusEl.textContent = "Upload started";
        else statusEl.textContent = "Upload failed: " + response.status;
    } catch (err) {
        statusEl.textContent = "Upload failed: " + err.message;
    }
}

// Load runs on page load
loadRuns();

// Add Refresh button functionality
document.getElementById('refreshBtn').addEventListener('click', loadRuns);
