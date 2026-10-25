/** Fail before importing AppModule/connecting Neon on retired hosting platforms. */
export function assertSupportedDeployment(env:NodeJS.ProcessEnv){
 if(env.RAILWAY_PROJECT_ID||env.RAILWAY_SERVICE_ID||env.RENDER==='true'||env.RENDER_SERVICE_ID)
  throw new Error('This hosting platform is retired. Corum Ranked production runs only on Singapore Lightsail.');
}
