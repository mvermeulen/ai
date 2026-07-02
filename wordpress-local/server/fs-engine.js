const fs = require('fs');
const path = require('path');
const matter = require('gray-matter');

const contentDir = path.join(__dirname, '../content');
const draftsDir = path.join(contentDir, 'drafts');
const publishedDir = path.join(contentDir, 'published');

// Ensure directories exist
[draftsDir, publishedDir].forEach(dir => {
    if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
});

const getFiles = (dir) => {
    if (!fs.existsSync(dir)) return [];
    return fs.readdirSync(dir)
        .filter(file => file.endsWith('.md'))
        .map(file => {
            const filePath = path.join(dir, file);
            const stat = fs.statSync(filePath);
            const fileContent = fs.readFileSync(filePath, 'utf-8');
            const parsed = matter(fileContent);
            
            return {
                filename: file,
                filepath: filePath,
                title: parsed.data.title || file.replace('.md', ''),
                wp_id: parsed.data.wp_id || null,
                status: parsed.data.status || 'draft',
                lastModified: stat.mtime,
                content: parsed.content,
                frontmatter: parsed.data
            };
        });
};

const fsEngine = {
    getAllPosts: () => {
        const drafts = getFiles(draftsDir).map(p => ({ ...p, folder: 'drafts' }));
        const published = getFiles(publishedDir).map(p => ({ ...p, folder: 'published' }));
        return [...drafts, ...published].sort((a, b) => b.lastModified - a.lastModified);
    },
    
    getPost: (filename, folder = 'drafts') => {
        const filePath = path.join(contentDir, folder, filename);
        if (!fs.existsSync(filePath)) return null;
        
        const stat = fs.statSync(filePath);
        const fileContent = fs.readFileSync(filePath, 'utf-8');
        const parsed = matter(fileContent);
        
        return {
            filename,
            filepath: filePath,
            folder,
            title: parsed.data.title || filename.replace('.md', ''),
            wp_id: parsed.data.wp_id || null,
            status: parsed.data.status || 'draft',
            lastModified: stat.mtime,
            content: parsed.content,
            frontmatter: parsed.data
        };
    },

    savePost: (filename, folder, data) => {
        const safeFilename = filename.endsWith('.md') ? filename : filename + '.md';
        const destFolder = folder === 'published' ? publishedDir : draftsDir;
        const filePath = path.join(destFolder, safeFilename);
        
        // Construct markdown with frontmatter
        const frontmatter = {
            title: data.title || safeFilename.replace('.md', ''),
            wp_id: data.wp_id || null,
            status: data.status || (folder === 'published' ? 'publish' : 'draft'),
            ...data.frontmatter
        };
        
        const fileContent = matter.stringify(data.content || '', frontmatter);
        fs.writeFileSync(filePath, fileContent, 'utf-8');
        
        return { filename: safeFilename, filepath: filePath, folder };
    },

    deletePost: (filename, folder) => {
        const filePath = path.join(contentDir, folder, filename);
        if (fs.existsSync(filePath)) {
            fs.unlinkSync(filePath);
            return true;
        }
        return false;
    }
};

module.exports = fsEngine;
