const axios = require('axios');
const fsEngine = require('./fs-engine');
const marked = require('marked');

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
    pushLocalChanges: async (dryRun = true) => {
        const posts = fsEngine.getAllPosts();
        const headers = getAuthHeaders();
        let logs = [];
        
        if (!headers) {
            logs.push("Warning: No credentials found in .env. Enforcing MOCK (Dry Run) mode.");
            dryRun = true;
        }

        for (const post of posts) {
            // Let's assume a basic rule: if it doesn't have a wp_id it's new.
            // If it does have a wp_id, it might be an update.
            const isNew = !post.wp_id;
            
            // Convert markdown to HTML for WordPress
            const htmlContent = marked.parse(post.content || '');
            
            const payload = {
                title: post.title,
                content: htmlContent,
                status: post.status,
            };

            if (dryRun) {
                logs.push(`[MOCK SYNC] File: ${post.filename} | Action: ${isNew ? 'CREATE' : 'UPDATE'} | Status: ${post.status}`);
                continue;
            }

            try {
                if (isNew) {
                    const res = await axios.post(`${API_BASE}/posts`, payload, { headers });
                    // Save wp_id back to frontmatter
                    fsEngine.savePost(post.filename, post.folder, {
                        ...post,
                        wp_id: res.data.id,
                        status: res.data.status
                    });
                    logs.push(`Successfully created "${post.title}" (ID: ${res.data.id})`);
                } else {
                    await axios.post(`${API_BASE}/posts/${post.wp_id}`, payload, { headers });
                    logs.push(`Successfully updated "${post.title}" (ID: ${post.wp_id})`);
                }
            } catch (err) {
                logs.push(`Error syncing "${post.title}": ${err.message}`);
            }
        }
        
        return { logs, dryRun };
    }
};

module.exports = syncEngine;
