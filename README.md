# DrukAlert console

Next.js console for TTI Rangjung's two-zone hazard monitor. The ESP32 publishes one JSON packet to HiveMQ. A worker stores it in PostgreSQL, and this app shows the gate and the river.

The zones are TTIR Main Gate (trespass) and River Alongside Railing (flood).

## Run

1. Copy `.env.example` to `.env` and replace `SESSION_SECRET`, `ADMIN_PASSWORD`, and `MQTT_INGEST_KEY` with long random values.
2. Start Postgres: `docker compose up -d`. The database is published on port **5433** so it does not collide with a local PostgreSQL install on 5432.
3. Install dependencies: `npm install`.
4. Apply the schema and seed the admin: `npm run db:migrate`, then `npm run db:seed`.
5. Start the app: `npm run dev`.
6. Start ingestion in a second terminal: `npm run worker`.
7. Open http://localhost:3000 and sign in as `admin` with `ADMIN_PASSWORD` from `.env`.

`npm run db:reset` drops the database, reapplies the migrations, and seeds again. Hazard events, readings, and zone status are cleared. The admin user and the two zones are recreated.

## Vercel and Prisma Postgres

The Vercel project uses the Prisma Postgres database `prisma-postgres-red-umbrella`. The integration prefixes its variables with `PSQL_DATABASE_`:

- `PSQL_DATABASE_DATABASE_URL_POOLED` — pooled connection, used by the console and the MQTT worker
- `PSQL_DATABASE_POSTGRES_URL` — direct connection, used by migrations and seed
- `PSQL_DATABASE_PRISMA_DATABASE_URL` — direct connection from the same integration

Do not commit those values. Vercel injects them into the deployment. When they are absent, the app and Prisma use `DATABASE_URL`, which is how local Docker keeps working.

Vercel runs `npm run vercel-build`. That generates the client, applies migrations through the direct URL, then builds Next.js.

The first deploy creates empty tables. Seed the admin into that database with the direct URL available in the environment (`PSQL_DATABASE_POSTGRES_URL` or `DATABASE_URL`), then run `npm run db:seed`. A local `.env` that only points at Docker will seed Docker instead.

The worker does not run on Vercel. Wherever you run `npm run worker`, that machine needs `PSQL_DATABASE_DATABASE_URL_POOLED`.

## Keep it running with PM2

PM2 is a process manager you install on the machine. It is not part of this project. Use it when you want the console and the worker to keep running after you close the terminal. Postgres still comes from Docker (`docker compose up -d`).

Install PM2 once:

```bash
npm install -g pm2
```

From the `web` folder, build the console, then hand both processes to PM2:

```bash
npm run build
pm2 start npm --name drukalert-web -- start
pm2 start npm --name drukalert-worker -- run worker
pm2 save
```

`drukalert-web` is `npm start` (the production console on port 3000). `drukalert-worker` is `npm run worker` (MQTT ingestion). `pm2 save` remembers that list.

Useful commands, run from any directory:

```bash
pm2 status
pm2 logs
pm2 logs drukalert-worker
pm2 restart drukalert-web
pm2 restart drukalert-worker
pm2 stop all
pm2 delete all
```

`pm2 stop` pauses them. `pm2 delete` removes them from PM2's list. After a code change, run `npm run build` again, then `pm2 restart drukalert-web`. The worker does not need a rebuild; `pm2 restart drukalert-worker` is enough.

To bring the same processes back after a Windows reboot, install the startup helper once, then save the process list:

```bash
npm install -g pm2-windows-startup
pm2-startup install
pm2 save
```

## What the console does

The board polls every 2 seconds. A zone is **Live** when a packet arrived in the last 12 seconds.

- An alert turns the zone card red, writes a hazard-log row, and plays a two-tone browser sound after the operator allows audio. The sound repeats until the alert is acknowledged. **Acknowledge** stores the signed-in username and the time, and tells the board to stop that zone’s buzzer. The first person to mark it seen is kept. **Stop buzzer** sends the same stop without marking the alert seen.
- When that zone returns to normal, the log gets a **Resolved** row. It says **Acknowledged first** or **Not acknowledged**, based on whether anyone had marked the alert seen before it cleared.
- A failed distance is **Sensor offline**. On the river, a failed humidity read is offline too. The header says **Sensor fault** while nothing is alerting. A blind gate sensor does not look clear.
- Each card draws a live graph for the last 15 minutes or the last hour. Time runs along the bottom, distance uses the left scale in centimetres, and humidity on the river uses the right scale from 0 to 100. Point at the line to read the exact time and values. Readings are saved about every 5 seconds, and also when the zone changes state, and kept for about an hour. The river card also shows a passing object.
- **Download CSV** exports the full hazard log, including who acknowledged each alert and when.

## Telegram

Alerts are sent only when both `TELEGRAM_BOT_TOKEN` and `TELEGRAM_CHAT_ID` are set. `TELEGRAM_CHAT_ID` may be a numeric chat id, or `auto` to use the latest chat that has messaged the bot. Open the bot once and send it any message before the first alert, or `auto` has no chat to deliver to.

`TELEGRAM_BOT_LINK` is the public bot address. It is shown on the sign-in page and the dashboard, and it is appended to each alert. 

## Firmware

Flash `iot/esp32_v2.ino`. Fill in `ssid`, `password`, and `MQTT_INGEST_KEY` at the top. The ingest key must be the same value as `MQTT_INGEST_KEY` in `.env`.

The board publishes every 2.5 seconds. Flood needs humidity at 40% or more, water between 2 and 50 cm, and a passing object. Trespass needs motion and a person between 10 and 90 cm. The board rings for 5 seconds on a flood and 13 seconds on a trespass. It listens on `drukalert/zone/cmd` for `stop_buzzer` plus the zone name. Set `MQTT_CMD_TOPIC` only if that topic changes.

The broker is public, so it will accept a packet from anyone. The worker drops any packet that does not carry this key. Reflash the board after changing the key, or live readings will be refused.

## Test without the board

`npm run simulate` publishes a loop of scenes every 3 seconds: clear, flood, trespass, approach, blind sensors, then both alerts.

Hold one scene with `npm run simulate -- flood`, or send a single packet with `npm run simulate -- clear --once`. Scenes: `clear`, `flood`, `trespass`, `approach`, `blind`, `all`.
