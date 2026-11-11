import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {expect,it} from 'vitest';

it('evaluates the coordinator policy: every main push deploys unless [no-deploy]; manual/markers retain their meanings',()=>{
 const source=readFileSync(new URL('../../../../.github/workflows/ranked-release.yml',import.meta.url),'utf8');
 const deploy=readFileSync(new URL('../../../../.github/workflows/deploy-ranked-lightsail.yml',import.meta.url),'utf8');
 const code=source.match(/python3 - <<'PY'\n([\s\S]*?)\n          PY/)![1]!.split('\n').map(l=>l.slice(10)).join('\n');
 const refExpression=source.split('\n').find(l=>l.trimStart().startsWith('if: github.ref'))!.trim().slice(4);
 const refGate=new Function('github',`return ${refExpression}`) as (github:unknown)=>boolean;
 const directory=mkdtempSync(join(tmpdir(),'corum-release-policy-'));
 try{
  const run=(message:string,auto='false',event='push',ref='refs/heads/main')=>{
   if(!refGate({ref}))return false;
   const output=join(directory,'output');writeFileSync(output,'');
   execFileSync('python3',['-c',code],{env:{...process.env,MESSAGE:message,PUBLISH:'false',AUTO_DEPLOY:auto,EVENT:event,GITHUB_OUTPUT:output}});
   return readFileSync(output,'utf8').includes('deploy=true');
  };
  expect(run('feat: overhaul [no-deploy] [build only]','true')).toBe(false);
  expect(run('[deploy-lightsail] [no-deploy]','true')).toBe(false);
  // Every main push deploys the server (2026-11-11); [no-deploy] still opts out.
  expect(run('ordinary push')).toBe(true);
  expect(run('ordinary push [no-deploy]')).toBe(false);
  expect(run('ordinary push','true')).toBe(true);
  expect(run('[deploy-lightsail]')).toBe(true);
  expect(run('[deploy-lightsail] [build only]')).toBe(true);
  expect(run('','false','workflow_dispatch')).toBe(true);
  expect(run('[deploy-lightsail]','true','push','refs/heads/feature')).toBe(false);
 }finally{rmSync(directory,{recursive:true,force:true});}
 expect(deploy).toContain('StrictHostKeyChecking=yes');expect(deploy).toContain('52.74.37.130');
});

