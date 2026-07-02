require('dotenv').config();
const express = require('express');
const cors = require('cors');
const path = require('path');
const multer = require('multer');
const db = require('./server/db');
const syncEngine = require('./server/sync');

const app = express();
const PORT = process.env.PORT || 3030;

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// Configure multer for file uploads
const storage = multer.diskStorage({
    destination: (req, file, cb) => {
        cb(null, 'public/uploads/')
    },
    filename: (req, file, cb) => {
        cb(null, Date.now() + '-' + file.originalname)
    }
});
const upload = multer({ storage: storage });

// --- API Endpoints ---

// Get all posts
app.get('/api/posts', async (req, res) => {
    try {
        const posts = await db.all('SELECT * FROM posts ORDER BY updated_at DESC');
        res.json(posts);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Get a single post
app.get('/api/posts/:id', async (req, res) => {
    try {
        const post = await db.get('SELECT * FROM posts WHERE id = ?', [req.params.id]);
        if (!post) return res.status(404).json({ error: 'Not found' });
        res.json(post);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Create a post
app.post('/api/posts', async (req, res) => {
    try {
        const { title, content, type, status } = req.body;
        const result = await db.run(
            'INSERT INTO posts (title, content, type, status, sync_status) VALUES (?, ?, ?, ?, ?)',
            [title, content || '', type || 'post', status || 'draft', 'local']
        );
        res.json({ id: result.lastID, message: 'Post created locally' });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Update a post
app.put('/api/posts/:id', async (req, res) => {
    try {
        const { title, content, type, status } = req.body;
        await db.run(
            'UPDATE posts SET title = ?, content = ?, type = ?, status = ?, sync_status = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?',
            [title, content, type, status, 'local_modified', req.params.id]
        );
        res.json({ message: 'Post updated locally' });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Delete a post
app.delete('/api/posts/:id', async (req, res) => {
    try {
        // If it has a wp_id, maybe we should mark it as deleted instead to sync the deletion?
        // For simplicity, we just delete locally now.
        await db.run('DELETE FROM posts WHERE id = ?', [req.params.id]);
        res.json({ message: 'Post deleted' });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Upload media
app.post('/api/media', upload.single('file'), async (req, res) => {
    try {
        if (!req.file) return res.status(400).json({ error: 'No file uploaded' });
        const localPath = '/uploads/' + req.file.filename;
        const result = await db.run(
            'INSERT INTO media (local_path, sync_status) VALUES (?, ?)',
            [localPath, 'local']
        );
        res.json({ id: result.lastID, localPath, message: 'File uploaded locally' });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Get media
app.get('/api/media', async (req, res) => {
    try {
        const media = await db.all('SELECT * FROM media ORDER BY uploaded_at DESC');
        res.json(media);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Sync endpoints
app.post('/api/sync/pull', async (req, res) => {
    try {
        const results = await syncEngine.pullPosts();
        res.json({ message: 'Pull completed', results });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

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
    console.log(`WPOffline server running on http://localhost:${PORT}`);
});
