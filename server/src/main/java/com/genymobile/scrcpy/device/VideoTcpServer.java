package com.genymobile.scrcpy.device;

import java.io.DataInputStream;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.security.MessageDigest;

/**
 * Direct TCP connection for the video stream (--video-tcp), bypassing the adb transport.
 * <p>
 * The server listens on a port of all the device network interfaces, so any host of the network may connect: a client is accepted only if
 * it sends the random token received through the server arguments (the adb channel) first. The first authenticated connection closes the
 * listener.
 */
public final class VideoTcpServer {

    public static final int TOKEN_LENGTH = 32;

    private static final int AUTH_TIMEOUT_MS = 2000;

    private VideoTcpServer() {
        // not instantiable
    }

    public static ServerSocket listen(int port) throws IOException {
        return new ServerSocket(port);
    }

    /**
     * Accept connections until one sends the expected token (then acknowledge it with one byte), or until the timeout expires.
     */
    public static Socket acceptAuthenticated(ServerSocket serverSocket, byte[] token, int timeoutMs) throws IOException {
        assert token.length == TOKEN_LENGTH;
        long deadline = System.currentTimeMillis() + timeoutMs;
        try {
            while (true) {
                int remaining = (int) (deadline - System.currentTimeMillis());
                if (remaining <= 0) {
                    throw new SocketTimeoutException("No authenticated video TCP connection");
                }
                serverSocket.setSoTimeout(remaining);
                Socket socket = serverSocket.accept();
                if (authenticate(socket, token)) {
                    return socket;
                }
                socket.close();
            }
        } finally {
            serverSocket.close();
        }
    }

    private static boolean authenticate(Socket socket, byte[] token) {
        try {
            socket.setSoTimeout(AUTH_TIMEOUT_MS);
            byte[] received = new byte[TOKEN_LENGTH];
            new DataInputStream(socket.getInputStream()).readFully(received);
            // Constant-time comparison
            if (!MessageDigest.isEqual(received, token)) {
                return false;
            }
            socket.setSoTimeout(0);
            socket.getOutputStream().write(0);
            return true;
        } catch (IOException e) {
            // Timeout or connection closed before sending the token
            return false;
        }
    }

    public static byte[] parseToken(String hex) {
        if (hex.length() != 2 * TOKEN_LENGTH) {
            throw new IllegalArgumentException("Invalid video TCP token length");
        }
        byte[] token = new byte[TOKEN_LENGTH];
        for (int i = 0; i < TOKEN_LENGTH; ++i) {
            int hi = Character.digit(hex.charAt(2 * i), 16);
            int lo = Character.digit(hex.charAt(2 * i + 1), 16);
            if (hi < 0 || lo < 0) {
                throw new IllegalArgumentException("Invalid video TCP token");
            }
            token[i] = (byte) (hi << 4 | lo);
        }
        return token;
    }
}
