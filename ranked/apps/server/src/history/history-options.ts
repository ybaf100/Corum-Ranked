export type HistoryOrder = 'NEWEST' | 'OLDEST';
export type HistoryResult = 'ALL' | 'WIN' | 'LOSS' | 'CHAMPION' | 'PODIUM' | 'ELIMINATED' | 'IN_PROGRESS';
export interface HistoryOptions { matchId?:string; summary?:boolean; order?: HistoryOrder|undefined; result?: HistoryResult|undefined; }
