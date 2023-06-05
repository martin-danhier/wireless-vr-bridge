package com.wvb.xr_client;

public class MainActivity extends android.app.NativeActivity {
  static {
    System.loadLibrary("openxr_loader");
    System.loadLibrary("wvb_xr_client");
  }

}
