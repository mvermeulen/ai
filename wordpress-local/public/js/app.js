const API_URL = 'http://localhost:4000/api';

// State
let saveTimeout = null;

// DOM Elements
const draftsList = document.getElementById('drafts-list');
const publishedList = document.getElementById('published-list');
const titleInput = document.getElementById('post-title');
const bodyInput = document.getElementById('post-body');
const currFilename = document.getElementById('current-filename');
const currFolder = document.getElementById('current-folder');
const metaStatus = document.getElementById('meta-status');
const metaWpId = document.getElementById('meta-wpid');
const saveStatus = document.getElementById('save-status');

const previewPanel = document.getElementById('preview-panel');
const previewContent = document.getElementById('preview-content');

// --- Initialization & Loading ---

async function loadSidebar() {
    try {
        const res = await fetch(`${API_URL}/posts`);
        const posts = await res.json();
        
        draftsList.innerHTML = '';
        publishedList.innerHTML = '';
        
        posts.forEach(post => {
            const li = document.createElement('li');
            li.innerHTML = `<i class="ph ph-file-text"></i> ${post.title || post.filename}`;
            li.onclick = () => loadPost(post.filename, post.folder);
            
            if (post.filename === currFilename.value && post.folder === currFolder.value) {
                li.classList.add('active');
            }
            
            if (post.folder === 'drafts') draftsList.appendChild(li);
            else publishedList.appendChild(li);
        });
    } catch (err) {
        console.error('Failed to load posts', err);
    }
}

async function loadPost(filename, folder) {
    try {
        const res = await fetch(`${API_URL}/posts/${folder}/${filename}`);
        if (!res.ok) throw new Error('Not found');
        const post = await res.json();
        
        currFilename.value = post.filename;
        currFolder.value = post.folder;
        titleInput.value = post.title;
        bodyInput.value = post.content;
        
        metaStatus.textContent = post.status;
        metaWpId.textContent = post.wp_id || 'None (New)';
        
        updatePreview();
        loadSidebar(); // refresh active state
    } catch (err) {
        console.error(err);
    }
}

// --- Auto-Save Logic ---

function triggerSave() {
    saveStatus.textContent = 'Saving...';
    clearTimeout(saveTimeout);
    saveTimeout = setTimeout(savePost, 1000);
    updatePreview();
}

async function savePost() {
    const payload = {
        filename: currFilename.value || `untitled-${Date.now()}`,
        folder: currFolder.value || 'drafts',
        title: titleInput.value,
        content: bodyInput.value,
        status: metaStatus.textContent,
        wp_id: metaWpId.textContent === 'None (New)' ? null : metaWpId.textContent
    };
    
    try {
        const res = await fetch(`${API_URL}/posts`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const data = await res.json();
        
        currFilename.value = data.post.filename;
        currFolder.value = data.post.folder;
        
        saveStatus.textContent = 'All changes saved to disk';
        loadSidebar();
    } catch (err) {
        saveStatus.textContent = 'Error saving!';
    }
}

titleInput.addEventListener('input', triggerSave);
bodyInput.addEventListener('input', triggerSave);

// --- New Post ---
document.getElementById('btn-new-post').addEventListener('click', () => {
    currFilename.value = '';
    currFolder.value = 'drafts';
    titleInput.value = '';
    bodyInput.value = '';
    metaStatus.textContent = 'draft';
    metaWpId.textContent = 'None (New)';
    titleInput.focus();
    updatePreview();
    loadSidebar();
});

// --- Preview Logic ---
function updatePreview() {
    if (!previewPanel.classList.contains('hidden')) {
        previewContent.innerHTML = marked.parse(bodyInput.value);
    }
}

document.getElementById('btn-view-preview').addEventListener('click', () => {
    previewPanel.classList.toggle('hidden');
    updatePreview();
});

document.getElementById('btn-close-preview').addEventListener('click', () => {
    previewPanel.classList.add('hidden');
});

// --- Sync Modal Logic ---
const syncModal = document.getElementById('sync-modal');
const syncLogs = document.getElementById('sync-logs');

document.getElementById('btn-sync').addEventListener('click', () => {
    syncLogs.textContent = 'Ready to sync...';
    syncModal.classList.add('active');
});

document.getElementById('btn-sync-close').addEventListener('click', () => {
    syncModal.classList.remove('active');
});

document.getElementById('btn-sync-run').addEventListener('click', async () => {
    syncLogs.textContent = 'Starting Mock Sync (Dry Run)...\n\n';
    try {
        const res = await fetch(`${API_URL}/sync/push`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ dryRun: true })
        });
        const data = await res.json();
        
        if (data.results.logs.length === 0) {
            syncLogs.textContent += 'Nothing to sync.';
        } else {
            data.results.logs.forEach(log => {
                syncLogs.textContent += log + '\n';
            });
        }
    } catch (err) {
        syncLogs.textContent += '\nError during sync!';
    }
});

// Boot
loadSidebar();
