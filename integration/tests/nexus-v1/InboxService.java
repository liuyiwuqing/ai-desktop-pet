package dev.nexus.core.inbox;

import java.util.UUID;

/** Database-side effect boundary; DeliveryService itself is compiled from nexus-core source. */
public final class InboxService {
    public UUID readMessage;
    public void markRead(UUID message) { readMessage = message; }
}
