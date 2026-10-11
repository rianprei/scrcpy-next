package com.genymobile.scrcpy.device;

import org.junit.Assert;
import org.junit.Test;

public class DeviceRotationTest {

    @Test
    public void testAutoRotationEnabled() {
        Assert.assertTrue(Device.isAutoRotationEnabled("1"));
    }

    @Test
    public void testRotationLocked() {
        Assert.assertFalse(Device.isAutoRotationEnabled("0"));
    }

    @Test
    public void testMissingSettingKeepsRotationLocked() {
        Assert.assertFalse(Device.isAutoRotationEnabled((String) null));
        Assert.assertFalse(Device.isAutoRotationEnabled("null"));
    }

    @Test
    public void testInvalidSettingKeepsRotationLocked() {
        Assert.assertFalse(Device.isAutoRotationEnabled(""));
        Assert.assertFalse(Device.isAutoRotationEnabled("invalid"));
        Assert.assertFalse(Device.isAutoRotationEnabled("1 "));
    }

    @Test
    public void testUnexpectedNumberKeepsRotationLocked() {
        Assert.assertFalse(Device.isAutoRotationEnabled("2"));
        Assert.assertFalse(Device.isAutoRotationEnabled("-1"));
    }
}
