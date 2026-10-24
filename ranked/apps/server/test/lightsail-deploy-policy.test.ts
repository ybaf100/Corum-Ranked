import {readFileSync} from 'node:fs';
import {expect,it} from 'vitest';

it('evaluates the real deployment gate: review-only pushes stay off even with auto-deploy, while manual and existing markers remain supported',()=>{
 const source=readFileSync(new URL('../../../../.github/workflows/deploy-ranked-lightsail.yml',import.meta.url),'utf8');
 const expression=source.split('\n').find(l=>l.trimStart().startsWith('if: github.ref'))!.trim().slice(4);
 const gate=new Function('github','vars','contains',`return ${expression}`) as (github:unknown,vars:unknown,contains:(a:string,b:string)=>boolean)=>boolean;
 const run=(message:string,auto='false',event='push',ref='refs/heads/main')=>gate({ref,event_name:event,event:{head_commit:{message}}},{LIGHTSAIL_AUTO_DEPLOY:auto},(a,b)=>a.includes(b));
 expect(run('feat: overhaul [no-deploy] [build only]','true')).toBe(false);
 expect(run('[deploy-lightsail] [no-deploy]','true')).toBe(false);
 expect(run('ordinary push')).toBe(false);
 expect(run('ordinary push','true')).toBe(true);
 expect(run('[deploy-lightsail]')).toBe(true);
 expect(run('','false','workflow_dispatch')).toBe(true);
 expect(run('[deploy-lightsail]','true','push','refs/heads/feature')).toBe(false);
 expect(source).toContain('StrictHostKeyChecking=yes');expect(source).toContain('52.74.37.130');
});
