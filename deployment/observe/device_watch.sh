#!/bin/sh
# dodo-1 在线状态观测：只读查询生产库 devices/device_telemetry_samples，每 5 分钟一条。
# 记录 fresh_online（online 且 90 秒内有活动）、last_seen、最近 RSSI、16 分钟内断开事件；
# 在线/离线翻转时附加 event 标记，便于回溯。不写库、不重启服务、不输出凭据。
set -u
DEV=dodo-1
DIR="$HOME/nexus-observe"
LOG="$DIR/device-$DEV.ndjson"
INTERVAL=300
mkdir -p "$DIR"; chmod 700 "$DIR"
printf '{"ts":"%s","event":"start","device":"%s","interval_s":%s,"log":"%s"}\n' \
  "$(date -u +%FT%TZ)" "$DEV" "$INTERVAL" "$LOG" >> "$LOG"

while :; do
  TS=$(date -u +%FT%TZ)
  PG_ID=$(docker ps -q \
    --filter "label=com.docker.compose.project=nexus-production" \
    --filter "label=com.docker.compose.service=postgres")
  ROW=$(docker exec "$PG_ID" psql -U nexus -d nexus -tAc \
    "select coalesce(row_to_json(r)::text,'null') from (select enabled, (online and last_seen > now()-interval '90 seconds') as fresh_online, last_seen, (select wifi_rssi from device_telemetry_samples s where s.device_id=d.device_id order by observed_at desc limit 1) as last_rssi, (select count(*) from device_telemetry_samples s where s.device_id=d.device_id and s.wifi_disconnect_reason is not null and s.observed_at > now()-interval '16 minutes') as disconnect_events_16m from devices d where device_id='$DEV') r" 2>/dev/null)
  if [ -n "$ROW" ] && [ "$ROW" != "null" ]; then
    ONLINE=$(printf '%s' "$ROW" | grep -o '"fresh_online":[a-z]*' | cut -d: -f2)
    if [ -n "${PREV_ONLINE:-}" ] && [ "$PREV_ONLINE" != "$ONLINE" ]; then
      EVENT="\"$ONLINE\""
    else
      EVENT=null
    fi
    PREV_ONLINE=$ONLINE
    printf '{"ts":"%s","event":%s,"state":%s}\n' "$TS" "$EVENT" "$ROW" >> "$LOG"
  else
    printf '{"ts":"%s","event":"query_failed","device":"%s"}\n' "$TS" "$DEV" >> "$LOG"
  fi
  sleep "$INTERVAL"
done
