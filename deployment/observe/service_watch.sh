#!/bin/sh
# ACC-01 24 小时观测：每 15 分钟记录一次，只读，不输出令牌。
# 令牌优先从受保护凭据文件读取，兼容旧的 NEXUS_ADMIN_TOKEN 环境变量。
set -u

DIR="$HOME/nexus-observe"
mkdir -p "$DIR"
chmod 700 "$DIR"
LOG="${LOG_FILE:-$DIR/observe-acc01.ndjson}"
DURATION=${DURATION_S:-86400}
INTERVAL=${INTERVAL_S:-900}
END=$(( $(date +%s) + DURATION ))
printf '{"ts":"%s","event":"start","duration_s":%s,"interval_s":%s,"log":"%s"}\n' "$(date -u +%FT%TZ)" "$DURATION" "$INTERVAL" "$LOG" >> "$LOG"

while [ "$(date +%s)" -lt "$END" ]; do
  TS=$(date -u +%FT%TZ)
  PROJECT=nexus-production
  CORE_ID=$(docker ps -q --filter "label=com.docker.compose.project=$PROJECT" --filter "label=com.docker.compose.service=core")
  PG_ID=$(docker ps -q --filter "label=com.docker.compose.project=$PROJECT" --filter "label=com.docker.compose.service=postgres")
  MQ_ID=$(docker ps -q --filter "label=com.docker.compose.project=$PROJECT" --filter "label=com.docker.compose.service=mqtt")

  if [ -n "$CORE_ID" ]; then
    CORE=$(docker inspect "$CORE_ID" --format '{{.State.Status}}/{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}/{{.RestartCount}}' 2>/dev/null)
    CORE_MEM=$(docker stats --no-stream --format '{{.MemUsage}}' "$CORE_ID" 2>/dev/null | tr -d '"')
    MQ_RECONNECT=$(docker logs --since 16m "$CORE_ID" 2>&1 | grep -ci reconnect || true)
    # docker cp 的输出是 tar 流，原先用 awk 在流里抓 "token" 依赖 JSON 排版；排版一变就取到空值，
    # status 整段变成 {"error":"unauthorized"}。改为落到临时文件后用 python3 解析结构，读完即删。
    TOKFILE=$(mktemp "$DIR/.token.XXXXXX")
    chmod 600 "$TOKFILE"
    TOKEN=
    if docker cp "$CORE_ID":/etc/nexus/core-auth/tokens.json "$TOKFILE" >/dev/null 2>&1; then
      TOKEN=$(python3 -c 'import json,sys;d=json.load(open(sys.argv[1]));print((d.get("admin") or {}).get("current",{}).get("token") or "")' "$TOKFILE" 2>/dev/null || true)
    fi
    rm -f "$TOKFILE"
    if [ -z "$TOKEN" ]; then
      TOKEN=$(docker inspect "$CORE_ID" --format '{{range .Config.Env}}{{println .}}{{end}}' | sed -n 's/^NEXUS_ADMIN_TOKEN=//p')
    fi
    if [ -n "$TOKEN" ]; then
      STATUS=$(curl -sS -m 5 -H "Authorization: Bearer $TOKEN" http://127.0.0.1:18088/api/v1/status 2>/dev/null || true)
    else
      STATUS=
    fi
    unset TOKEN
  else
    CORE=missing
    CORE_MEM=
    MQ_RECONNECT=0
    STATUS=
  fi
  case "$STATUS" in
    \{*) ;;
    *) STATUS=null ;;
  esac

  if [ -n "$PG_ID" ]; then
    PG=$(docker inspect "$PG_ID" --format '{{.State.Status}}/{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}/{{.RestartCount}}' 2>/dev/null)
    PG_CONN=$(docker exec "$PG_ID" psql -U nexus -d nexus -tAc 'select count(*) from pg_stat_activity' 2>/dev/null | tr -d ' ')
  else
    PG=missing
    PG_CONN=null
  fi

  if [ -n "$MQ_ID" ]; then
    MQ=$(docker inspect "$MQ_ID" --format '{{.State.Status}}/{{.RestartCount}}' 2>/dev/null)
  else
    MQ=missing
  fi

  DISK_AVAIL=$(df -Pk / | awk 'NR==2{printf "%s", $4}')
  DISK_PCT=$(df -Pk / | awk 'NR==2{gsub("%","",$5); printf "%s", $5}')
  HTTP5XX=$(docker exec openresty sh -c 'tail -n 200 /www/sites/pet/log/access.log' 2>/dev/null | awk '$9 ~ /^5/ {c++} END{print c+0}')
  HTTPS_CERT=$(echo | openssl s_client -connect 127.0.0.1:443 -servername pet.muyin.site 2>/dev/null | openssl x509 -noout -enddate 2>/dev/null | cut -d= -f2)
  MQTT_CERT=$(echo | openssl s_client -connect 127.0.0.1:8883 -servername pet.muyin.site 2>/dev/null | openssl x509 -noout -enddate 2>/dev/null | cut -d= -f2)

  printf '{"ts":"%s","core":"%s","pg":"%s","mqtt":"%s","status":%s,"disk_avail_kb":%s,"disk_used_pct":%s,"pg_conn":%s,"core_mem":"%s","mqtt_reconnect_16m":%s,"http5xx_last200":%s,"https_cert_not_after":"%s","mqtt_cert_not_after":"%s"}\n' \
    "$TS" "$CORE" "$PG" "$MQ" "$STATUS" "${DISK_AVAIL:-null}" "${DISK_PCT:-null}" "${PG_CONN:-null}" "${CORE_MEM:-}" "${MQ_RECONNECT:-0}" "${HTTP5XX:-0}" "${HTTPS_CERT:-}" "${MQTT_CERT:-}" >> "$LOG"

  sleep "$INTERVAL"
done

printf '{"ts":"%s","event":"stop"}\n' "$(date -u +%FT%TZ)" >> "$LOG"
