package dev.nexus.core.delivery;

import dev.nexus.core.inbox.InboxService;
import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.Timestamp;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.springframework.jdbc.core.JdbcTemplate;
import tools.jackson.databind.ObjectMapper;

/** Runs the actual Java producer/consumer against the actual C firmware service.
 * Only transport, SQL and InboxService side effects are simulated. No server is started. */
public final class ContractProbe {
    private static final UUID MESSAGE = UUID.fromString("11111111-2222-4333-8444-555555555555");
    private static final UUID EVENT = UUID.fromString("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");

    private static final class BoundaryDb extends JdbcTemplate {
        String status = "sent";
        int transitions;
        int reads;
        Boolean online;
        final Timestamp expires = Timestamp.from(Instant.now().plusSeconds(3600));

        @Override public List<Map<String, Object>> queryForList(String sql, Object... args) {
            if (sql.contains("SELECT d.event_id,d.device_id")) {
                return List.of(Map.of("event_id", EVENT, "device_id", "dodo-01", "message_id", MESSAGE,
                    "attempts", 0, "expires_at", expires, "source", "Halo_Blog", "event_type", "comment.created",
                    "title", "新评论\n带引号\"与 emoji 🐾".repeat(8),
                    "summary", "访客：你好，桌宠。\t测试转义\\与 UTF-8。".repeat(12), "priority", "high"));
            }
            if (sql.contains("SELECT d.status,d.attempts")) {
                ++reads;
                if (!EVENT.equals(args[0]) || !MESSAGE.equals(args[1]) || !"dodo-01".equals(args[2]))
                    return List.of();
                return List.of(Map.of("status", status, "attempts", 1, "expires_at", expires));
            }
            throw new AssertionError("unexpected query: " + sql);
        }

        @Override public int update(String sql, Object... args) {
            if (sql.contains("UPDATE deliveries SET status=?,last_error=NULL,received_at")) {
                check(EVENT.equals(args[3]), "ACK event identity");
                check(args[0].equals(args[1]) && args[0].equals(args[2]), "ACK state timestamps");
                status = (String) args[0];
                ++transitions;
            } else if (sql.contains("UPDATE deliveries SET status=?,last_error=?")) {
                check(EVENT.equals(args[2]), "rejection event identity");
                status = (String) args[0];
                ++transitions;
            } else if (sql.contains("UPDATE devices SET online=false")) {
                online = false;
            } else if (sql.contains("UPDATE devices SET online=true,last_seen")) {
                online = true;
            }
            return 1;
        }

        @Override public int update(String sql) { return update(sql, new Object[0]); }
    }

    private static void check(boolean condition, String label) {
        if (!condition) throw new AssertionError(label);
    }

    public static void main(String[] args) throws Exception {
        var mapper = new ObjectMapper();
        var db = new BoundaryDb();
        var inbox = new InboxService();
        var service = new DeliveryService(db, 5, 2, inbox);
        if (args[0].equals("emit")) {
            var transport = new MqttTransport();
            new DeliveryWorker(service, transport, mapper).dispatch();
            check(transport.payload != null, "DeliveryWorker produced an envelope");
            Files.write(Path.of(args[1]), transport.payload);
            System.out.println("nexus source: DeliveryWorker emitted " + transport.payload.length + " UTF-8 bytes");
            return;
        }
        var lines = Files.readAllLines(Path.of(args[1]));
        check(lines.size() == 7, "all firmware packets emitted");
        var received = mapper.readTree(lines.get(0));
        int reads = db.reads;
        service.acknowledge("wrong-device", received);
        check(db.reads == reads && db.transitions == 0, "wrong topic device rejected before SQL");
        String[] states = {"received", "displayed", "read"};
        for (int i = 0; i < 3; ++i) {
            service.acknowledge("dodo-01", mapper.readTree(lines.get(i)));
            check(db.status.equals(states[i]) && db.transitions == i + 1, "accepted " + states[i]);
        }
        check(MESSAGE.equals(inbox.readMessage), "read reaches the original inbox message");
        service.acknowledge("dodo-01", mapper.readTree(lines.get(3)));
        service.acknowledge("dodo-01", received);
        check(db.status.equals("read") && db.transitions == 3, "duplicate/backwards states ignored");
        db.status = "sent";
        service.acknowledge("dodo-01", mapper.readTree(lines.get(4)));
        check(db.status.equals("expired") && db.transitions == 4, "expired rejection accepted");
        db.status = "cancelled";
        service.acknowledge("dodo-01", received);
        check(db.status.equals("cancelled") && db.transitions == 4, "cancelled delivery cannot be revived by an ACK");
        service.state("dodo-01", mapper.readTree(lines.get(5)));
        check(Boolean.TRUE.equals(db.online), "online observedAt accepted");
        service.state("dodo-01", mapper.readTree(lines.get(6)));
        check(Boolean.FALSE.equals(db.online), "offline LWT accepted");
        System.out.println("nexus source: received/displayed/read/replay/expired/cancelled and online/offline accepted");
    }
}
