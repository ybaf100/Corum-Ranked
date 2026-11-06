-- Players pick up to six files (Geode logs and crash logs) per upload.
-- Widens the file index range only; no rows are changed.
ALTER TABLE ranked_debug_log_chunks DROP CONSTRAINT IF EXISTS ranked_debug_log_chunks_file_index_check;
ALTER TABLE ranked_debug_log_chunks ADD CONSTRAINT ranked_debug_log_chunks_file_index_check CHECK (file_index BETWEEN 0 AND 5);
