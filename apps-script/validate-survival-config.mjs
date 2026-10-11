import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import { test } from 'node:test';

class Range {
  constructor(sheet, row, column, rows = 1, columns = 1) { Object.assign(this, { sheet, row, column, rows, columns }); }
  getValues() { return Array.from({length:this.rows}, (_, r) => Array.from({length:this.columns}, (_, c) => this.sheet.rows[this.row+r-1]?.[this.column+c-1] ?? '')); }
  getDisplayValues() { return this.getValues().map(row => row.map(String)); }
  setValues(values) { values.forEach((row,r) => row.forEach((value,c) => { this.sheet.rows[this.row+r-1] ??= []; this.sheet.rows[this.row+r-1][this.column+c-1] = value; })); return this; }
}
class Sheet {
  constructor(name, rows=[]) { this.name=name; this.rows=structuredClone(rows); }
  getName() { return this.name; }
  getLastRow() { return this.rows.length; }
  getLastColumn() { return Math.max(0,...this.rows.map(row=>row.length)); }
  getRange(...args) { return new Range(this,...args); }
  getDataRange() { return this.getRange(1,1,Math.max(1,this.getLastRow()),Math.max(1,this.getLastColumn())); }
  appendRow(row) { this.rows.push([...row]); }
  setFrozenRows() {}
}
function fixture() {
  const sheets=[new Sheet('Ranked Config', [['Key','Value','설명'],['enabled',true,'Solo'],['operator-key',123,'keep']]), new Sheet('sheet1',[['existing'],['unchanged']])];
  const spreadsheet={ getSheetByName:name=>sheets.find(sheet=>sheet.name===name) ?? null, insertSheet(name) { const sheet=new Sheet(name); sheets.push(sheet); return sheet; } };
  const context=vm.createContext({ console, SpreadsheetApp:{getActiveSpreadsheet:()=>spreadsheet}, PropertiesService:{getScriptProperties:()=>({getProperty:()=>''})}, ContentService:{ MimeType:{JSON:'application/json'}, createTextOutput:text=>({text,setMimeType(){return this;}}) } });
  for (const name of ['Code.gs','RankedConfig.gs','SurvivalConfig.gs']) vm.runInContext(fs.readFileSync(new URL(name,import.meta.url),'utf8'),context);
  const set=(key,value)=>{ const row=spreadsheet.getSheetByName('Survival Config').rows.find(row=>row[0]===key); assert.ok(row); row[1]=value; };
  const document=()=>JSON.parse(context.doGet({parameter:{action:'survival_config'}}).text).data;
  return { context,spreadsheet,sheets,set,document };
}
test('setup creates independent defaults, preserves Solo and is repeatable',()=>{
  const f=fixture(), old=structuredClone(f.sheets.map(sheet=>sheet.rows));
  assert.equal(f.context.setupCorumSurvivalConfig().valid,true);
  const d=f.document();
  assert.equal(d.validation.valid,true);
  assert.deepEqual(d.config, {enabled:false,initialLp:150,placementEnabled:false,promotionSeriesEnabled:false,demotionProtectionEnabled:true,matchPlayerCount:16,minimumPlayers:8,matchmakingMaxWaitSeconds:180,tierMatchingEnabled:false,roundSeconds:180,round1Qualifiers:10,round2Qualifiers:6,round3Qualifiers:3,finalWinners:1,allowCutoffTies:true,lastAttemptEnabled:false,scoreMultiplier:1,tiebreakAttempts:3,tiebreakDecimals:3,minimumLevelId:40000000,demotionWarningRecoveryLp:50,demotionTargetOffsetLp:50,tierBands:['RED','AQUA','BRONZE','SILVER','GOLD'].map((tier,index)=>({tier,group:tier,minimumLp:index*300})),placementLp:[50,40,30,25,20,15,10,8,5,2,-10,-20,-30,-40,-50,-55]});
  const count=f.spreadsheet.getSheetByName('Survival Config').rows.length;
  f.set('enabled',true); f.set('initialLp',175);
  f.context.setupCorumSurvivalConfig();
  assert.equal(f.document().config.initialLp,175); assert.equal(f.document().config.enabled,true);
  assert.equal(f.spreadsheet.getSheetByName('Survival Config').rows.length,count);
  assert.deepEqual(f.sheets.slice(0,2).map(sheet=>sheet.rows),old);
});
test('partial setup keeps values and respects reordered headers',()=>{
  const f=fixture(); f.sheets.push(new Sheet('Survival Config',[['Value','Key','설명'],[true,'enabled','keep'],[222,'initialLp','keep']]));
  assert.equal(f.context.setupCorumSurvivalConfig().valid,true);
  assert.equal(f.document().config.initialLp,222); assert.equal(f.document().config.enabled,true);
  assert.equal(f.sheets[2].rows[1][2],'keep');
});
test('missing sheet and malformed values produce validation errors without changing data',()=>{
  const f=fixture(); assert.equal(f.document().validation.valid,false); f.context.setupCorumSurvivalConfig();
  for (const [key,value] of [['enabled','typo'],['minimumPlayers',7],['matchmakingMaxWaitSeconds',0],['placementEnabled',true],['promotionSeriesEnabled',true],['lastAttemptEnabled',true],['round2Qualifiers',12],['placementLp','[50]'],['tierBands','bad JSON']]) {
    const old=structuredClone(f.sheets[2].rows); f.set(key,value);
    assert.equal(f.document().validation.valid,false,key); f.sheets[2].rows=old;
  }
});
test('subdivided tiers use explicit icon groups and duplicate keys are rejected',()=>{
  const f=fixture(); f.context.setupCorumSurvivalConfig();
  f.set('tierBands','[{"tier":"RED_I","group":"RED","minimumLp":0},{"tier":"RED_II","group":"RED","minimumLp":100},{"tier":"AQUA","group":"AQUA","minimumLp":300}]');
  assert.equal(f.document().validation.valid,true);
  f.sheets[2].appendRow(['enabled',true,'duplicate']);
  assert.equal(f.document().validation.valid,false);
});
