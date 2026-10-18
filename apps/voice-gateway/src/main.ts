import { pino } from 'pino';
import { loadConfig } from './config.js';
import { createGateway } from './server.js';

const config = loadConfig();
const log = pino({ level: config.logLevel, redact: ['token', '*.token'] });
const gateway = createGateway(config, log);
gateway.server.listen(config.port, config.host, () => {
  log.info({ host: config.host, port: config.port, path: config.path }, 'AKENO voice gateway listening');
});

for (const signal of ['SIGINT', 'SIGTERM'] as const) {
  process.once(signal, () => {
    log.info({ signal }, 'shutting down');
    void gateway.close().finally(() => process.exit(0));
  });
}
