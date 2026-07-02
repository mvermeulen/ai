const axios = require('axios');
const db = require('./db');
const TurndownService = require('turndown');
const turndownService = new TurndownService();

// We expect WP_URL, WP_USER, WP_APP_PASSWORD in .env
const WP_URL = process.env.WP_URL || 'https://mvermeulen.org/gone2look4america';
const API_BASE = `${WP_URL}/wp-json/wp/v2`;

const getAuthHeaders = () => {
    if (!process.env.WP_USER || !process.env.WP_APP_PASSWORD) {
        return null;
    }
    const token = Buffer.from(`${process.env.WP_USER}:${process.env.WP_APP_PASSWORD}`).toString('base64');
    return { 'Authorization': `Basic ${token}` };
};

const syncEngine = {
    pullPosts: async () => {
        // Fetch posts from WP and save locally
        try {
            console.log(`Pulling from ${API_BASE}/posts`);
            const response = await axios.get(`${API_BASE}/posts`);
            const wpPosts = response.data;
            let results = { added: 0, updated: 0, skipped: 0 };

            for (const post of wpPosts) {
                // Convert HTML content to Markdown for local editing
                const markdownContent = turndownService.turndown(post.content.rendered);
                
                const existing = await db.get('SELECT * FROM posts WHERE wp_id = ?', [post.id]);
                if (existing) {
                    if (existing.sync_status === 'local_modified') {
                        // Conflict! We skip pulling to not overwrite local changes
                        results.skipped++;
                    } else {
                        await db.run(
                            'UPDATE posts SET title = ?, content = ?, status = ?, sync_status = ? WHERE id = ?',
                            [post.title.rendered, markdownContent, post.status, 'synced', existing.id]
                        );
                        results.updated++;
                    }
                } else {
                    await db.run(
                        'INSERT INTO posts (wp_id, title, content, type, status, sync_status) VALUES (?, ?, ?, ?, ?, ?)',
                        [post.id, post.title.rendered, markdownContent, post.type, post.status, 'synced']
                    );
                    results.added++;
                }
            }
            return results;
        } catch (error) {
            console.error("Error pulling posts:", error.message);
            throw error;
        }
    },

    pushLocalChanges: async (dryRun = false) => {
        // Get all locally modified or new posts
        const localPosts = await db.all("SELECT * FROM posts WHERE sync_status IN ('local', 'local_modified')");
        const headers = getAuthHeaders();
        let logs = [];
        
        if (!headers) {
            logs.push("Warning: No WP_USER or WP_APP_PASSWORD in .env. Falling back to MOCK mode.");
            dryRun = true;
        }

        for (const post of localPosts) {
            const isNew = !post.wp_id;
            
            // Note: in a real app, we would convert Markdown back to HTML here using Marked
            // For now, we assume the backend handles it or we send the raw HTML.
            // Let's require marked to parse it before sending:
            const marked = require('marked');
            const htmlContent = marked.parse(post.content);

            const payload = {
                title: post.title,
                content: htmlContent,
                status: post.status,
            };

            if (dryRun) {
                logs.push(`[DRY RUN] Would ${isNew ? 'CREATE' : 'UPDATE'} post: "${post.title}" with payload:`, payload);
                continue;
            }

            try {
                if (isNew) {
                    const res = await axios.post(`${API_BASE}/posts`, payload, { headers });
                    await db.run('UPDATE posts SET wp_id = ?, sync_status = ? WHERE id = ?', [res.data.id, 'synced', post.id]);
                    logs.push(`Successfully created post "${post.title}"`);
                } else {
                    await axios.post(`${API_BASE}/posts/${post.wp_id}`, payload, { headers });
                    await db.run('UPDATE posts SET sync_status = ? WHERE id = ?', ['synced', post.id]);
                    logs.push(`Successfully updated post "${post.title}"`);
                }
            } catch (err) {
                logs.push(`Error syncing "${post.title}": ${err.message}`);
            }
        }
        
        return { logs, dryRun };
    }
};

module.exports = syncEngine;
