let runsList = [];

async function loadRuns() {
    try {
        const response = await fetch('/runs');
        const runs = await response.json();
        runsList = runs || [];
        const container = document.getElementById('runsContainer');

        if (!runsList || runsList.length === 0) {
            container.innerHTML = "<p>No runs recorded yet.</p>";
        } else {
            let html = "<ul>";
            runsList.forEach((run, index) => {
                html += `<li>
                            <span>${run.name} (${run.size} bytes)</span>
                            <button onclick="uploadRun(${index})">Upload</button>
                         </li>`;
            });
            html += "</ul>";
            container.innerHTML = html;
        }
    } catch (error) {
        console.error("Failed to load runs:", error);
        document.getElementById('runsContainer').innerHTML = "<p>Error loading runs.</p>";
    }
}

async function uploadRun(index) {
    const statusEl = document.getElementById('uploadStatus');
    if (!runsList || !runsList[index]) {
        statusEl.textContent = "Invalid run selected";
        return;
    }
    const runName = runsList[index].name;
    statusEl.textContent = `Uploading ${runName}...`;

    try {
        const body = 'run=' + encodeURIComponent(runName);
        const response = await fetch('/uploadRun', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: body
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
