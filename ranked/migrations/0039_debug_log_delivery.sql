-- Debug log uploads are delivered to Discord as .log files; the chunks are
-- only a transport buffer and are removed once delivered.
ALTER TABLE ranked_debug_log_chunks ADD COLUMN IF NOT EXISTS total_files INTEGER NOT NULL DEFAULT 1;
