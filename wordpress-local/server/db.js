const sqlite3 = require('sqlite3').verbose();
const path = require('path');
const fs = require('fs');

const dataDir = path.join(__dirname, '../data');
if (!fs.existsSync(dataDir)) {
    fs.mkdirSync(dataDir);
}

const db = new sqlite3.Database(path.join(dataDir, 'wpoffline.db'));

db.serialize(() => {
    // Posts table (stores both posts and pages)
    db.run(`CREATE TABLE IF NOT EXISTS posts (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        wp_id INTEGER,
        title TEXT NOT NULL,
        content TEXT,
        type TEXT DEFAULT 'post',
        status TEXT DEFAULT 'draft',
        sync_status TEXT DEFAULT 'local', 
        updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
    )`);

    // Media table
    db.run(`CREATE TABLE IF NOT EXISTS media (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        wp_id INTEGER,
        local_path TEXT NOT NULL,
        wp_url TEXT,
        sync_status TEXT DEFAULT 'local',
        uploaded_at DATETIME DEFAULT CURRENT_TIMESTAMP
    )`);

    // Settings table
    db.run(`CREATE TABLE IF NOT EXISTS settings (
        key TEXT PRIMARY KEY,
        value TEXT
    )`);
});

// Helper wrapper for promises
const dbPromise = {
    all: (sql, params = []) => new Promise((resolve, reject) => {
        db.all(sql, params, (err, rows) => {
            if (err) reject(err); else resolve(rows);
        });
    }),
    get: (sql, params = []) => new Promise((resolve, reject) => {
        db.get(sql, params, (err, row) => {
            if (err) reject(err); else resolve(row);
        });
    }),
    run: (sql, params = []) => new Promise((resolve, reject) => {
        db.run(sql, params, function(err) {
            if (err) reject(err); else resolve(this);
        });
    })
};

module.exports = dbPromise;
