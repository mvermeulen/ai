// State Management
let currentTab = 'max-value';

// Helper: Format large numbers with commas
function formatBigInt(str) {
    if (!str || str === "0") return "0";
    if (str.length > 21) {
        return str; // Return as-is for extremely large numbers to preserve exact digits
    }
    return str.replace(/\B(?=(\d{3})+(?!\d))/g, ",");
}

// Helper: Format seconds to readable duration
function formatDuration(sec) {
    if (sec === null || sec === undefined || !isFinite(sec) || sec < 0) return '--';
    if (sec < 60) return `${sec.toFixed(1)}s`;
    
    const d = Math.floor(sec / 86400);
    const h = Math.floor((sec % 86400) / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = Math.floor(sec % 60);
    
    let parts = [];
    if (d > 0) parts.push(`${d}d`);
    if (h > 0) parts.push(`${h}h`);
    if (d === 0 && m > 0) parts.push(`${m}m`);
    if (d === 0 && h === 0 && s > 0) parts.push(`${s}s`);
    
    return parts.join(' ') || '0s';
}

// Tab Switching
window.switchTab = function(tabName) {
    currentTab = tabName;
    document.querySelectorAll('.tab-btn').forEach(btn => btn.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(content => content.classList.remove('active'));
    
    // Find active button
    const activeBtn = Array.from(document.querySelectorAll('.tab-btn')).find(btn => 
        btn.getAttribute('onclick').includes(tabName)
    );
    if (activeBtn) activeBtn.classList.add('active');
    
    const activeContent = document.getElementById(`${tabName}-tab`);
    if (activeContent) activeContent.classList.add('active');
};

// Poll controller status
async function updateDashboard() {
    try {
        const response = await fetch('/api/status');
        if (!response.ok) throw new Error('Network response not ok');
        const data = await response.json();
        
        // 1. Update overall status badge
        const badge = document.getElementById('global-status-badge');
        if (data.is_running) {
            badge.textContent = 'Searching';
            badge.className = 'badge running';
            document.getElementById('start-btn').disabled = true;
            document.getElementById('stop-btn').disabled = false;
            toggleInputs(true);
        } else {
            badge.textContent = 'Idle';
            badge.className = 'badge idle';
            document.getElementById('start-btn').disabled = false;
            document.getElementById('stop-btn').disabled = true;
            toggleInputs(false);
        }

        // 2. Update throughput & aggregate metrics
        const speed = data.progress.combined_throughput_m_s || 0;
        document.getElementById('header-throughput').textContent = `${speed.toFixed(2)} M/s`;
        
        // Calculate ETA
        const endNum = data.task.end_num;
        const etaContainer = document.getElementById('eta-container');
        if (data.is_running && speed > 0 && endNum !== "1000000000000000000000000000000") {
            try {
                const remainingNums = BigInt(endNum) - BigInt(data.progress.next_search_num) + 1n;
                const speedNumsPerSec = Number(speed) * 1000000;
                if (remainingNums > 0n) {
                    const remainingSecs = Number(remainingNums) / speedNumsPerSec;
                    document.getElementById('header-eta').textContent = formatDuration(remainingSecs);
                    etaContainer.style.display = 'flex';
                } else {
                    document.getElementById('header-eta').textContent = '--';
                    etaContainer.style.display = 'flex';
                }
            } catch (e) {
                etaContainer.style.display = 'none';
            }
        } else {
            etaContainer.style.display = 'none';
        }
        
        document.getElementById('stats-checked').textContent = formatBigInt(data.progress.total_numbers_checked.toString());
        document.getElementById('stats-steps').textContent = formatBigInt(data.progress.total_steps_computed.toString());
        document.getElementById('stats-time').textContent = formatDuration(data.progress.elapsed_seconds);
        document.getElementById('stats-next-num').textContent = formatBigInt(data.progress.next_search_num);

        // 3. Update Progress Bar
        const progressPercent = data.progress.percent_completed || 0;
        document.getElementById('progress-fill').style.width = `${progressPercent}%`;
        document.getElementById('progress-percent').textContent = `${progressPercent.toFixed(2)}%`;

        // 4. Render Worker Nodes
        renderWorkers(data.workers, data.task.backend);

        // 5. Render Peaks Tables (Sorting descending to show newest peaks first)
        renderPeaksTable('max-value-peaks-body', data.global_peaks.max_value_peaks, 'max_value');
        renderPeaksTable('steps-peaks-body', data.global_peaks.steps_peaks, 'steps');
        renderPeaksTable('sigma-peaks-body', data.global_peaks.sigma_peaks, 'sigma');

    } catch (error) {
        console.error('Error fetching dashboard status:', error);
    }
}

// Enable/Disable form inputs
function toggleInputs(disabled) {
    document.getElementById('start-num').disabled = disabled;
    document.getElementById('end-num').disabled = disabled;
    document.getElementById('backend').disabled = disabled;
    document.getElementById('cutoff-width').disabled = disabled;
    document.getElementById('target-duration').disabled = disabled;
}

// Render cluster worker list
function renderWorkers(workers, activeBackend) {
    const container = document.getElementById('workers-container');
    const keys = Object.keys(workers);
    
    document.getElementById('workers-count').textContent = `${keys.length} Node${keys.length === 1 ? '' : 's'}`;
    
    if (keys.length === 0) {
        container.innerHTML = '<div class="empty-state">No workers registered. Add one below or launch with --workers.</div>';
        return;
    }
    
    // Prevent blinking effect by reusing existing DOM elements instead of full innerHTML replace
    Array.from(container.children).forEach(child => {
        if (child.classList.contains('empty-state')) {
            child.remove();
            return;
        }
        const addr = child.getAttribute('data-addr');
        if (!workers[addr]) {
            child.remove();
        }
    });

    keys.forEach(addr => {
        const w = workers[addr];
        const statusClass = w.status === 'online' ? 'online' : (w.status === 'busy' ? 'busy' : 'offline');
        const loadStr = w.system_load ? w.system_load[0].toFixed(2) : '0.00';
        
        let benchmarkHtml = '';
        if (w.backends) {
            Object.keys(w.backends).forEach(b => {
                let speed = w.backends[b];
                
                if (w.throughput_history && w.throughput_history[b] && w.throughput_history[b].length > 0) {
                    const hist = w.throughput_history[b];
                    speed = hist.reduce((sum, val) => sum + val, 0) / hist.length;
                }
                
                if (speed !== null) {
                    const activeClass = b === activeBackend && w.status === 'busy' ? 'active' : '';
                    let label = b.toUpperCase();
                    if (b === 'cpu_domain') label = 'CPU (DS)';
                    else if (b === 'vulkan_domain') label = 'VULKAN (DS)';
                    else if (b === 'hip_domain') label = 'HIP (DS)';
                    benchmarkHtml += `<span class="bench-tag ${activeClass}">${label}: ${speed.toFixed(1)} M/s</span>`;
                }
            });
        }
        
        let activeJobHtml = '';
        if (w.status === 'busy' && w.active_job) {
            const job = w.active_job;
            const elapsed = Math.floor(Date.now() / 1000 - job.start_time);
            activeJobHtml = `
                <div class="worker-job-info">
                    Job: ${job.job_id.substring(0, 12)}...<br>
                    Range: [${formatBigInt(job.start_num.toString())}, ${formatBigInt(job.end_num.toString())}]<br>
                    Running for ${elapsed}s
                </div>
            `;
        }

        const htmlContent = `
            <div class="worker-header">
                <div class="worker-identity">
                    <span class="worker-addr">${addr}</span>
                    <span class="worker-cores">${w.cpu_cores || '?'} Cores | Load: ${loadStr}</span>
                </div>
                <div style="display: flex; align-items: center; gap: 8px;">
                    <span class="status-indicator">
                        <span class="dot ${statusClass}"></span>
                        ${w.status.toUpperCase()}
                    </span>
                    <button onclick="removeWorker('${addr}')" class="btn btn-icon" style="color: #ff4b4b; background: transparent; border: none; font-size: 16px; cursor: pointer; padding: 0 4px;" title="Remove worker">&times;</button>
                </div>
            </div>
            ${activeJobHtml}
            <div class="worker-body">
                <div class="worker-benchmarks">
                    ${benchmarkHtml || '<span class="bench-tag">No backends discovered</span>'}
                </div>
            </div>
        `;
        
        let card = document.querySelector(`.worker-card[data-addr="${addr.replace(/:/g, '\\:')}"]`);
        if (!card) {
            card = document.createElement('div');
            card.className = 'worker-card animate-fade-in';
            card.setAttribute('data-addr', addr);
            container.appendChild(card);
        }
        card.innerHTML = htmlContent;
    });
}

// Render specific peaks table
function renderPeaksTable(tableId, peaks, metricType) {
    const tbody = document.getElementById(tableId);
    if (!peaks || peaks.length === 0) {
        tbody.innerHTML = `<tr><td colspan="2" class="empty-table">No peaks found. Start search to see results.</td></tr>`;
        return;
    }

    // Sort descending by start_val (using string length and string comparison for safe bigint sort)
    const sortedPeaks = [...peaks].sort((a, b) => {
        const lenA = a.start_val.length;
        const lenB = b.start_val.length;
        if (lenA !== lenB) return lenB - lenA;
        return b.start_val.localeCompare(a.start_val);
    });

    let html = '';
    sortedPeaks.forEach(peak => {
        html += `
            <tr>
                <td>${formatBigInt(peak.start_val)}</td>
                <td>${formatBigInt(peak.metric_val)}</td>
            </tr>
        `;
    });
    tbody.innerHTML = html;
}

// Event Listeners
document.getElementById('config-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    
    const startStr = document.getElementById('start-num').value;
    const endStr = document.getElementById('end-num').value;

    function parseNum(s) {
        if (!s || s.trim() === '') return null;
        s = s.replace(/,/g, '').trim();
        const pMatch = s.match(/^(\d+)\s*(?:\^|\*\*)\s*(\d+)$/);
        if (pMatch) {
            try { return (BigInt(pMatch[1]) ** BigInt(pMatch[2])).toString(); } catch(e) {}
        }
        if (/^\d+e\d+$/i.test(s)) return BigInt(Number(s)).toString();
        try { return BigInt(s).toString(); } catch(e) { return null; }
    }

    const startNum = parseNum(startStr);
    const endNum = parseNum(endStr);

    const payload = {
        backend: document.getElementById('backend').value,
        cutoff_width: parseInt(document.getElementById('cutoff-width').value),
        target_duration: parseFloat(document.getElementById('target-duration').value)
    };
    if (startNum) payload.start_num = startNum;
    if (endNum) payload.end_num = endNum;

    try {
        const response = await fetch('/api/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const result = await response.json();
        if (!response.ok) throw new Error(result.error || 'Failed to start search');
        
        console.log('Search started successfully:', result);
        updateDashboard();
    } catch (err) {
        alert(`Error starting search: ${err.message}`);
    }
});

document.getElementById('stop-btn').addEventListener('click', async () => {
    try {
        const response = await fetch('/api/stop', { method: 'POST' });
        const result = await response.json();
        if (!response.ok) throw new Error(result.error || 'Failed to stop search');
        
        console.log('Search stopped successfully:', result);
        updateDashboard();
    } catch (err) {
        alert(`Error stopping search: ${err.message}`);
    }
});

document.getElementById('daemon-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const addressInput = document.getElementById('daemon-address');
    const address = addressInput.value.trim();
    if (!address) return;

    try {
        const response = await fetch('/api/daemons', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ address })
        });
        const result = await response.json();
        if (!response.ok) throw new Error(result.error || 'Failed to add worker');
        
        console.log('Worker registered successfully:', result);
        addressInput.value = '';
        updateDashboard();
    } catch (err) {
        alert(`Error registering worker: ${err.message}`);
    }
});

window.removeWorker = async function(address) {
    if (!confirm(`Are you sure you want to remove worker ${address}?`)) return;
    try {
        const response = await fetch('/api/daemons/remove', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ address })
        });
        if (!response.ok) throw new Error('Failed to remove worker');
        updateDashboard();
    } catch (err) {
        alert(`Error removing worker: ${err.message}`);
    }
};

// Initial load & Polling Loop
updateDashboard();
setInterval(updateDashboard, 1000);

// Help Modal Controls
window.openHelp = function() {
    document.getElementById('help-modal').style.display = 'flex';
};

window.closeHelp = function() {
    document.getElementById('help-modal').style.display = 'none';
};

// Close modal when clicking outside content area
window.addEventListener('click', (e) => {
    const modal = document.getElementById('help-modal');
    if (e.target === modal) {
        closeHelp();
    }
});
