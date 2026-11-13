import "reflect-metadata";
import { Logger, ValidationPipe } from "@nestjs/common";
import { NestFactory } from "@nestjs/core";
import {assertSupportedDeployment} from "./config/deployment-target.js";
import { requestRateLimitMiddleware } from "./common/request-rate-limit.js";
import {
  SERVER_ENVIRONMENT,
  type ServerEnvironment,
} from "./config/server-environment.js";

const bootstrap = async (): Promise<void> => {
  const { AppModule } = await import("./app.module.js");
  const app = await NestFactory.create(AppModule, { abortOnError: true });
  const environment = app.get<ServerEnvironment>(SERVER_ENVIRONMENT);
  app.enableShutdownHooks();
  // Per-client budgets run before routing, body parsing of the request is still
  // bounded by Express (100 kB JSON) and Caddy.
  app.use(requestRateLimitMiddleware());
  app.useGlobalPipes(
    new ValidationPipe({
      whitelist: true,
      forbidNonWhitelisted: true,
      transform: true,
    }),
  );
  app.enableCors({
    origin: environment.corsOrigins.length > 0 ? [...environment.corsOrigins] : false,
    methods: ["GET", "POST"],
  });
  await app.listen(environment.port, "0.0.0.0");
  Logger.log(`Corum Ranked server listening on ${environment.port}`, "Bootstrap");
};

assertSupportedDeployment(process.env);
await bootstrap();
