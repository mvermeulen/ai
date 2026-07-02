const API_URL = 'http://localhost:3030/api';

// State
let currentPosts = [];
let currentEditorId = null;

// DOM Elements
const views = document.querySelectorAll('.view');
const navLinks = document.querySelectorAll('.nav-links a');
const postsTable = document.querySelector('#posts-table tbody');

// Navigation Logic
navLinks.forEach(link => {
    link.addEventListener('click', (e) => {
        e.preventDefault();
        const targetView = link.id.replace('nav-', 'view-');
        
        // Update Nav
        navLinks.forEach(l => l.classList.remove('active'));
        link.classList.add('active');
        
        // Update Views
        views.forEach(v => v.classList.remove('active'));
        document.getElementById(targetView).classList.add('active');
        
        if (targetView === 'view-dashboard') loadDashboard();
    });
});

// Toast Notification
function showToast(message, type = 'success') {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = 'toast';
    
    const icon = type === 'success' ? '<i class="ph ph-check-circle" style="color:var(--success)"></i>' :
                 '<i class="ph ph-warning-circle" style="color:var(--danger)"></i>';
                 
    toast.innerHTML = `${icon} <span>${message}</span>`;
    container.appendChild(toast);
    
    setTimeout(() => {
        toast.style.opacity = '0';
        setTimeout(() => toast.remove(), 300);
    }, 3000);
}

// --- Dashboard ---
async function loadDashboard() {
    try {
        const res = await fetch(`${API_URL}/posts`);
        currentPosts = await res.json();
        
        let localCount = 0;
        let syncedCount = 0;
        
        postsTable.innerHTML = '';
        currentPosts.forEach(post => {
            if (post.sync_status === 'synced') syncedCount++;
            else localCount++;
            
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td><strong>${post.title || '(Untitled)'}</strong></td>
                <td>${post.status}</td>
                <td><span class="badge ${post.sync_status}">${post.sync_status}</span></td>
                <td>
                    <button class="btn btn-secondary btn-sm" onclick="editPost(${post.id})"><i class="ph ph-pencil-simple"></i></button>
                    <button class="btn btn-secondary btn-sm" style="color:var(--danger)" onclick="deletePost(${post.id})"><i class="ph ph-trash"></i></button>
                </td>
            `;
            postsTable.appendChild(tr);
        });
        
        document.getElementById('stat-local').textContent = localCount;
        document.getElementById('stat-synced').textContent = syncedCount;
    } catch (err) {
        showToast('Failed to load posts', 'error');
    }
}

// --- Editor ---
const editorTitle = document.getElementById('editor-title');
const editorContent = document.getElementById('editor-content');
const editorPreview = document.getElementById('editor-preview');

editorContent.addEventListener('input', () => {
    editorPreview.innerHTML = marked.parse(editorContent.value);
});

async function editPost(id) {
    try {
        const res = await fetch(`${API_URL}/posts/${id}`);
        const post = await res.json();
        
        currentEditorId = post.id;
        editorTitle.value = post.title;
        editorContent.value = post.content;
        editorPreview.innerHTML = marked.parse(post.content);
        
        document.getElementById('nav-editor').click();
    } catch (err) {
        showToast('Error loading post', 'error');
    }
}

document.getElementById('btn-editor-cancel').addEventListener('click', () => {
    currentEditorId = null;
    editorTitle.value = '';
    editorContent.value = '';
    editorPreview.innerHTML = '';
    document.getElementById('nav-dashboard').click();
});

document.getElementById('btn-editor-save').addEventListener('click', async () => {
    const payload = {
        title: editorTitle.value,
        content: editorContent.value,
        status: 'draft' // For simplicity
    };
    
    try {
        const method = currentEditorId ? 'PUT' : 'POST';
        const url = currentEditorId ? `${API_URL}/posts/${currentEditorId}` : `${API_URL}/posts`;
        
        await fetch(url, {
            method,
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        
        showToast('Post saved locally!');
        document.getElementById('nav-dashboard').click();
    } catch (err) {
        showToast('Error saving post', 'error');
    }
});

async function deletePost(id) {
    if (!confirm('Are you sure you want to delete this locally?')) return;
    try {
        await fetch(`${API_URL}/posts/${id}`, { method: 'DELETE' });
        showToast('Post deleted');
        loadDashboard();
    } catch (err) {
        showToast('Error deleting post', 'error');
    }
}

// --- Sync Logic ---
document.getElementById('btn-pull-wp').addEventListener('click', async () => {
    try {
        showToast('Pulling from WordPress...', 'success');
        const res = await fetch(`${API_URL}/sync/pull`, { method: 'POST' });
        const data = await res.json();
        showToast(`Pull complete: ${data.results.added} added, ${data.results.updated} updated.`);
        loadDashboard();
    } catch (err) {
        showToast('Pull failed', 'error');
    }
});

const syncModal = document.getElementById('sync-modal');
const syncLogs = document.getElementById('sync-logs');

document.getElementById('btn-sync-all').addEventListener('click', () => {
    syncLogs.textContent = 'Ready to sync...';
    syncModal.classList.add('active');
});

document.querySelector('.close-modal').addEventListener('click', () => {
    syncModal.classList.remove('active');
});

async function runSync(dryRun) {
    syncLogs.textContent = `Starting ${dryRun ? 'Dry Run' : 'Sync'}...\n`;
    try {
        const res = await fetch(`${API_URL}/sync/push`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ dryRun })
        });
        const data = await res.json();
        
        if (data.results.logs.length === 0) {
            syncLogs.textContent += 'No local changes to sync.';
        } else {
            data.results.logs.forEach(log => {
                syncLogs.textContent += log + '\n';
            });
        }
        
        if (!dryRun) loadDashboard();
    } catch (err) {
        syncLogs.textContent += '\nError during sync!';
    }
}

document.getElementById('btn-sync-dryrun').addEventListener('click', () => runSync(true));
document.getElementById('btn-sync-confirm').addEventListener('click', () => runSync(false));

// --- Strava Tools ---
document.getElementById('btn-generate-strava').addEventListener('click', () => {
    const input = document.getElementById('strava-input').value;
    let routeId = '';
    
    // Extract ID from URL or use direct ID
    const match = input.match(/routes\/(\d+)/);
    if (match) {
        routeId = match[1];
    } else if (/^\d+$/.test(input.trim())) {
        routeId = input.trim();
    }
    
    if (!routeId) {
        showToast('Invalid Strava URL or ID', 'error');
        return;
    }
    
    const embedCode = `
<div class="strava-embed-placeholder" data-embed-type="route" data-embed-id="${routeId}" data-style="standard" data-map-hash=""></div>
<script src="https://strava-embeds.com/embed.js"></script>
`;
    
    document.getElementById('strava-code').value = embedCode;
    document.getElementById('strava-result-container').classList.remove('hidden');
});

document.getElementById('btn-copy-strava').addEventListener('click', () => {
    const code = document.getElementById('strava-code');
    code.select();
    document.execCommand('copy');
    showToast('Copied to clipboard!');
});

// Init
loadDashboard();
