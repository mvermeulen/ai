require('dotenv').config();
const express = require('express');
const cors = require('cors');
const path = require('path');
const fsEngine = require('./server/fs-engine');
const syncEngine = require('./server/sync-engine');

const app = express();
const PORT = process.env.PORT || 4000; // Using 4000 for V2

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// --- API Endpoints ---

// Get all posts
app.get('/api/posts', (req, res) => {
    try {
        const posts = fsEngine.getAllPosts();
        res.json(posts);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Get a single post
app.get('/api/posts/:folder/:filename', (req, res) => {
    try {
        const post = fsEngine.getPost(req.params.filename, req.params.folder);
        if (!post) return res.status(404).json({ error: 'Not found' });
        res.json(post);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Create or Update a post
app.post('/api/posts', (req, res) => {
    try {
        const { filename, folder, title, content, wp_id, status } = req.body;
        const name = filename || `untitled-${Date.now()}`;
        const destFolder = folder || 'drafts';
        
        const saved = fsEngine.savePost(name, destFolder, { title, content, wp_id, status });
        res.json({ message: 'Saved to filesystem', post: saved });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Delete a post
app.delete('/api/posts/:folder/:filename', (req, res) => {
    try {
        const deleted = fsEngine.deletePost(req.params.filename, req.params.folder);
        if (deleted) res.json({ message: 'File deleted' });
        else res.status(404).json({ error: 'File not found' });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Sync push
app.post('/api/sync/push', async (req, res) => {
    try {
        const dryRun = req.body.dryRun || false;
        const results = await syncEngine.pushLocalChanges(dryRun);
        res.json({ message: dryRun ? 'Dry run completed' : 'Push completed', results });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.listen(PORT, () => {
    console.log(`WPOffline V2 File-System CMS running on http://localhost:${PORT}`);
});
