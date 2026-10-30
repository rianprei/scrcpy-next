package com.genymobile.scrcpy.device;

import org.junit.Assert;
import org.junit.Test;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicReference;

public class VideoTcpServerTest {

    private static byte[] token(int seed) {
        byte[] token = new byte[VideoTcpServer.TOKEN_LENGTH];
        Arrays.fill(token, (byte) seed);
        return token;
    }

    private static Socket connect(int port, byte[] token) throws IOException {
        Socket socket = new Socket(InetAddress.getLoopbackAddress(), port);
        OutputStream out = socket.getOutputStream();
        out.write(token);
        out.flush();
        return socket;
    }

    @Test
    public void testRejectWrongTokenThenAcceptRightOne() throws Exception {
        ServerSocket serverSocket = new ServerSocket(0, 50, InetAddress.getLoopbackAddress());
        int port = serverSocket.getLocalPort();
        byte[] expected = token(42);

        AtomicReference<Object> result = new AtomicReference<>();
        Thread thread = new Thread(() -> {
            try {
                result.set(VideoTcpServer.acceptAuthenticated(serverSocket, expected, 5000));
            } catch (IOException e) {
                result.set(e);
            }
        });
        thread.start();

        try (Socket intruder = connect(port, token(7))) {
            // The server closes the connection without acknowledging it
            Assert.assertEquals(-1, intruder.getInputStream().read());
        }

        try (Socket client = connect(port, expected)) {
            InputStream in = client.getInputStream();
            Assert.assertEquals(0, in.read()); // acknowledgment
            thread.join();
            Assert.assertTrue(result.get() instanceof Socket);
            ((Socket) result.get()).close();
        }

        // The listener is closed after the first authenticated connection
        Assert.assertTrue(serverSocket.isClosed());
    }

    @Test
    public void testTimeout() throws Exception {
        ServerSocket serverSocket = new ServerSocket(0, 50, InetAddress.getLoopbackAddress());
        try {
            VideoTcpServer.acceptAuthenticated(serverSocket, token(1), 200);
            Assert.fail("Expected a timeout");
        } catch (SocketTimeoutException e) {
            // expected
        }
        Assert.assertTrue(serverSocket.isClosed());
    }

    @Test
    public void testParseToken() {
        StringBuilder hex = new StringBuilder();
        for (int i = 0; i < VideoTcpServer.TOKEN_LENGTH; ++i) {
            hex.append("0f");
        }
        Assert.assertArrayEquals(token(15), VideoTcpServer.parseToken(hex.toString()));

        try {
            VideoTcpServer.parseToken("00");
            Assert.fail();
        } catch (IllegalArgumentException e) {
            // expected
        }
        try {
            VideoTcpServer.parseToken(hex.toString().replace('f', 'g'));
            Assert.fail();
        } catch (IllegalArgumentException e) {
            // expected
        }
    }
}
