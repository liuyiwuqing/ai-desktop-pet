package dev.nexus.core.delivery;

import org.eclipse.paho.client.mqttv3.MqttException;

/** Contract-test transport boundary: captures bytes without opening a broker connection. */
public final class MqttTransport {
    byte[] payload;
    public boolean ready() { return true; }
    public void publish(String device, byte[] bytes) throws MqttException {
        if (!device.equals("dodo-01") || bytes.length > 1024) throw new AssertionError("wire bounds");
        payload = bytes;
    }
}
