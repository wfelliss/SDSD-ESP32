async function loadRuns() {
    try {
        const response = await fetch('/runs');
        const runs = await response.json();
        const container = document.getElementById('runsContainer');
        
        if (runs.length === 0) {
            container.innerHTML = "<p>No runs recorded yet.</p>";
        } else {
            let html = "<ul>";
            runs.forEach((run, index) => {
                html += `<li>
                            <span>Run ${index + 1} (${run.length} samples)</span>
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

document.getElementById('uploadBtn').addEventListener('click', async () => {
    const statusEl = document.getElementById('uploadStatus');
    statusEl.textContent = "Uploading...";

    try {
        const response = await fetch('/uploadRun', { method: 'POST' });
        if (response.ok) statusEl.textContent = "Upload successful!";
        else statusEl.textContent = "Upload failed: " + response.status;
    } catch (err) {
        statusEl.textContent = "Upload failed: " + err.message;
    }
});

// Load runs on page load
loadRuns();

// Add Refresh button functionality
document.getElementById('refreshBtn').addEventListener('click', loadRuns);
