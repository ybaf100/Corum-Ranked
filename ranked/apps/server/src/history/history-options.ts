export type HistoryOrder = 'NEWEST' | 'OLDEST';
export type HistoryResult = 'ALL' | 'WIN' | 'LOSS' | 'CHAMPION' | 'PODIUM' | 'ELIMINATED' | 'IN_PROGRESS';
/** everyone: Debug build only — every stored record on the server, not just the caller's. */
export interface HistoryOptions { matchId?:string; summary?:boolean; everyone?:boolean; order?: HistoryOrder|undefined; result?: HistoryResult|undefined; }
