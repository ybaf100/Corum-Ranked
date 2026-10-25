import {describe,it,expect} from 'vitest';
import {assertSupportedDeployment} from '../src/config/deployment-target.js';
describe('Lightsail-only bootstrap',()=>{
 it('allows local tests and Lightsail without extra database or networking probes',()=>{
  expect(()=>assertSupportedDeployment({NODE_ENV:'test'})).not.toThrow();
  expect(()=>assertSupportedDeployment({NODE_ENV:'production'})).not.toThrow();
 });
 it('refuses every retired platform before starting the game app or connecting Neon',()=>{
  for(const env of [{RAILWAY_PROJECT_ID:'retired'},{RAILWAY_SERVICE_ID:'retired'},{RENDER:'true'},{RENDER_SERVICE_ID:'retired'}])expect(()=>assertSupportedDeployment(env)).toThrow('retired');
 });
});
