package com.epicgames.ue4;

import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.ImageFormat;
import android.graphics.SurfaceTexture;
import android.hardware.camera2.*;
import android.hardware.camera2.CameraCharacteristics;
import android.media.Image;
import android.media.ImageReader;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Environment;
import android.util.Log;
import android.util.SizeF;
import android.view.Surface;
import java.nio.ByteBuffer;
import java.util.Arrays;
import android.Manifest;
import org.json.JSONArray;
import org.json.JSONObject;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.List;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

public class Camera2Helper {
    private static final String TAG = "Camera2Helper";
    private static final int CAMERA_PERMISSION_REQUEST_CODE = 100;
    private static Camera2Helper instance;
    
    private Context context;
    private CameraManager cameraManager;
    private CameraDevice cameraDevice;
    private CameraCaptureSession captureSession;
    private ImageReader imageReader;
    private HandlerThread backgroundThread;
    private Handler backgroundHandler;
    
    // Frame data storage
    private byte[] latestFrameData;
    private int frameWidth = 1280;
    private int frameHeight = 960;
    private boolean isCapturing = false;
    
    // Native callback
    private static native void onFrameAvailable(byte[] data, int width, int height);
    private static native void onIntrinsicsAvailable(float fx, float fy, float cx, float cy, float skew, int width, int height);
    private static native void onDistortionAvailable(float[] coeffs, int length);
    private static native void onOriginalResolutionAvailable(int width, int height);
    private static native void onPixelArraySizeAvailable(int width, int height);
    private static native void onActiveArraySizeAvailable(int width, int height);
    private static native void onCharacteristicsDumpAvailable(String json);
    private static native void onCameraSelected(String cameraId, boolean isLeftCamera);
    private static native void onCameraPoseAvailable(float tx, float ty, float tz, float qx, float qy, float qz, float qw);
    
    private Camera2Helper(Context ctx) {
        this.context = ctx;
        this.cameraManager = (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);
    }
    
    public static Camera2Helper getInstance(Context ctx) {
        Log.d(TAG, "Camera2Helper.getInstance called");
        if (instance == null) {
            Log.d(TAG, "Creating new Camera2Helper instance");
            instance = new Camera2Helper(ctx);
            Log.d(TAG, "Camera2Helper instance created successfully");
        } else {
            Log.d(TAG, "Using existing Camera2Helper instance");
        }
        return instance;
    }
    
    // Helper method to check camera permission
    private boolean checkCameraPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (context instanceof Activity) {
                Activity activity = (Activity) context;
                return activity.checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED;
            }
        }
        return true; // Permission not needed for older versions
    }
    
    // Helper method to request camera permission
    private void requestCameraPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (context instanceof Activity) {
                Activity activity = (Activity) context;
                Log.d(TAG, "Requesting CAMERA permission from user");
                activity.requestPermissions(new String[]{Manifest.permission.CAMERA}, CAMERA_PERMISSION_REQUEST_CODE);
            }
        }
    }
    
    public boolean startCamera() {
        Log.d(TAG, ">>> startCamera called");
        try {
            if (isCapturing) {
                Log.w(TAG, "Camera already started - returning true");
                return true;
            }
            
            // Check and request camera permission
            if (!checkCameraPermission()) {
                Log.w(TAG, "Camera permission not granted, requesting permission...");
                requestCameraPermission();
                return false; // Will need to retry after permission is granted
            }
            
            Log.d(TAG, "Camera permission granted, proceeding...");
            Log.d(TAG, "Starting background thread...");
            startBackgroundThread();
            Log.d(TAG, "Background thread started");
            
            Log.d(TAG, "Getting camera ID list...");
            String[] cameraIds = cameraManager.getCameraIdList();
            Log.d(TAG, "Found " + cameraIds.length + " cameras");
            
            // Also try manually checking for additional camera IDs
            Log.d(TAG, "=== EXHAUSTIVE CAMERA SEARCH ===");
            for (int testId = 0; testId < 10; testId++) {
                try {
                    CameraCharacteristics testChar = cameraManager.getCameraCharacteristics(String.valueOf(testId));
                    Log.d(TAG, "Manual check: Camera ID " + testId + " exists!");
                } catch (Exception e) {
                    Log.v(TAG, "Manual check: Camera ID " + testId + " not found");
                }
            }
            
            if (cameraIds.length == 0) {
                Log.e(TAG, "No cameras found");
                return false;
            }
            
            // Check all cameras and their characteristics - prioritize Quest 3 special cameras
            String cameraId = null;
            boolean isLeftCamera = false;
            
            // First pass: look for Quest 3 special cameras (50 = left, 51 = right)
            boolean has50 = false;
            boolean has51 = false;
            for (String id : cameraIds) {
                if (id.equals("50")) has50 = true;
                if (id.equals("51")) has51 = true;
            }
            
            // Use the camera based on preference (set via setPreferredCamera)
            Log.d(TAG, "Camera preference: " + (preferLeftCamera ? "LEFT" : "RIGHT"));
            
            if (preferLeftCamera) {
                // Prefer left camera (50)
                if (has50) {
                    cameraId = "50";
                    isLeftCamera = true;
                    Log.d(TAG, "Selected Quest 3 LEFT camera (ID 50) - matches preference");
                } else if (has51) {
                    cameraId = "51";
                    isLeftCamera = false;
                    Log.d(TAG, "Selected Quest 3 RIGHT camera (ID 51) - fallback (left not available)");
                }
            } else {
                // Prefer right camera (51)
                if (has51) {
                    cameraId = "51";
                    isLeftCamera = false;
                    Log.d(TAG, "Selected Quest 3 RIGHT camera (ID 51) - matches preference");
                } else if (has50) {
                    cameraId = "50";
                    isLeftCamera = true;
                    Log.d(TAG, "Selected Quest 3 LEFT camera (ID 50) - fallback (right not available)");
                }
            }
            
            if (cameraId != null) {
                try {
                    CameraCharacteristics characteristics = cameraManager.getCameraCharacteristics(cameraId);
                    Integer facing = characteristics.get(CameraCharacteristics.LENS_FACING);
                    String facingStr = "UNKNOWN";
                    if (facing != null) {
                        if (facing == CameraCharacteristics.LENS_FACING_FRONT) facingStr = "FRONT";
                        else if (facing == CameraCharacteristics.LENS_FACING_BACK) facingStr = "BACK";
                        else if (facing == CameraCharacteristics.LENS_FACING_EXTERNAL) facingStr = "EXTERNAL";
                    }
                    Log.d(TAG, "Quest 3 Camera " + cameraId + " facing: " + facingStr + " (isLeft=" + isLeftCamera + ")");
                } catch (Exception e) {
                    Log.w(TAG, "Could not get characteristics for camera " + cameraId + ": " + e.getMessage());
                }
            }
            
            // Second pass: if no special camera found, use regular logic
            if (cameraId == null) {
                for (String id : cameraIds) {
                    if (id.equals("50") || id.equals("51")) {
                        continue; // Skip special cameras (already checked)
                    }
                Log.d(TAG, "=== Camera ID: " + id + " ===");
                try {
                    CameraCharacteristics characteristics = cameraManager.getCameraCharacteristics(id);
                    Integer facing = characteristics.get(CameraCharacteristics.LENS_FACING);
                    String facingStr = "UNKNOWN";
                    if (facing != null) {
                        if (facing == CameraCharacteristics.LENS_FACING_FRONT) facingStr = "FRONT";
                        else if (facing == CameraCharacteristics.LENS_FACING_BACK) facingStr = "BACK";
                        else if (facing == CameraCharacteristics.LENS_FACING_EXTERNAL) facingStr = "EXTERNAL";
                    }
                    Log.d(TAG, "Camera " + id + " facing: " + facingStr + " (value=" + facing + ")");
                    
                    // Try to find any working camera - standard logic for non-special cameras
                    if (facing != null && facing == CameraCharacteristics.LENS_FACING_FRONT && cameraId == null) {
                        cameraId = id;
                        Log.d(TAG, "Selected FRONT camera with ID: " + id);
                    } else if (facing != null && facing == CameraCharacteristics.LENS_FACING_BACK && cameraId == null) {
                        cameraId = id;
                        Log.d(TAG, "Selected BACK camera with ID: " + id + " (fallback)");
                    } else if (cameraId == null) {
                        // If no camera found yet, use this as fallback
                        cameraId = id;
                        Log.d(TAG, "Using camera ID " + id + " as fallback (" + facingStr + ")");
                    }
                } catch (Exception e) {
                    Log.w(TAG, "Could not get characteristics for camera " + id + ": " + e.getMessage());
                }
            }
            }
            
            // If no front camera found, try ID 0 first, then use first available
            if (cameraId == null) {
                Log.d(TAG, "No suitable camera found in list, trying ID 0 manually...");
                try {
                    CameraCharacteristics char0 = cameraManager.getCameraCharacteristics("0");
                    cameraId = "0";
                    Log.d(TAG, "Manually selected camera ID 0");
                } catch (Exception e) {
                    Log.w(TAG, "Camera ID 0 manual test failed: " + e.getMessage());
                    if (cameraIds.length > 0) {
                        cameraId = cameraIds[0];
                        Log.d(TAG, "Fallback to first available: " + cameraId);
                    }
                }
            } else {
                Log.d(TAG, "Selected camera ID: " + cameraId);
            }
            // Remember selection for dumps
            this.currentCameraId = cameraId;
            this.currentIsLeftCamera = isLeftCamera;
            
            // Notify native side which camera was selected
            try {
                onCameraSelected(cameraId, isLeftCamera);
                Log.d(TAG, "Notified native: camera=" + cameraId + " isLeft=" + isLeftCamera);
            } catch (Exception e) {
                Log.w(TAG, "Failed to notify camera selection: " + e.getMessage());
            }
            
            // Trigger an immediate dump of characteristics for this camera
            try { dumpCameraCharacteristics(); } catch (Exception e) { Log.w(TAG, "Auto dump failed: " + e.getMessage()); }
            
            // Query intrinsics for selected camera (if available)
            try {
                CameraCharacteristics cc = cameraManager.getCameraCharacteristics(cameraId);
                float[] intr = cc.get(CameraCharacteristics.LENS_INTRINSIC_CALIBRATION);
                float fx = 0, fy = 0, cx = 0, cy = 0, skew = 0;
                if (intr != null && intr.length >= 4) {
                    fx = intr[0]; fy = intr[1]; cx = intr[2]; cy = intr[3];
                    if (intr.length >= 5) { skew = intr[4]; }
                    Log.d(TAG, "Intrinsics found: fx="+fx+" fy="+fy+" cx="+cx+" cy="+cy+" skew="+skew);
                } else {
                    Log.w(TAG, "LENS_INTRINSIC_CALIBRATION not available or too short");
                }

                // Also try focal lengths in pixels if available
                float[] focalLengthsMm = cc.get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS);
                SizeF sensorSizeMm = cc.get(CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE);
                Integer pixelArrayW = cc.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE) != null ?
                        cc.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE).getWidth() : null;
                Integer pixelArrayH = cc.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE) != null ?
                        cc.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE).getHeight() : null;
                if ((fx == 0 || fy == 0) && focalLengthsMm != null && focalLengthsMm.length > 0 && sensorSizeMm != null && pixelArrayW != null && pixelArrayH != null) {
                    float pixelsPerMmX = pixelArrayW / sensorSizeMm.getWidth();
                    float pixelsPerMmY = pixelArrayH / sensorSizeMm.getHeight();
                    fx = focalLengthsMm[0] * pixelsPerMmX;
                    fy = focalLengthsMm[0] * pixelsPerMmY;
                    cx = pixelArrayW * 0.5f;
                    cy = pixelArrayH * 0.5f;
                    Log.d(TAG, "Derived intrinsics from focal length: fx="+fx+" fy="+fy+" cx="+cx+" cy="+cy);
                }

                // Send original sensor/active-array resolution so UE can scale intrinsics
                int srcW = 0, srcH = 0;
                try {
                    android.util.Size pixelArray = cc.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE);
                    if (pixelArray != null) {
                        srcW = pixelArray.getWidth();
                        srcH = pixelArray.getHeight();
                        Log.d(TAG, "Pixel array size: " + srcW + "x" + srcH);
                        onPixelArraySizeAvailable(srcW, srcH);
                    }
                } catch (Exception e) {
                    Log.w(TAG, "SENSOR_INFO_PIXEL_ARRAY_SIZE unavailable: " + e.getMessage());
                }

                if (srcW == 0 || srcH == 0) {
                    try {
                        android.graphics.Rect active = cc.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE);
                        if (active != null) {
                            srcW = active.width();
                            srcH = active.height();
                            Log.d(TAG, "Active array size: " + srcW + "x" + srcH);
                            onActiveArraySizeAvailable(srcW, srcH);
                        }
                    } catch (Exception e) {
                        Log.w(TAG, "ACTIVE_ARRAY_SIZE unavailable: " + e.getMessage());
                    }
                }

                if (srcW > 0 && srcH > 0) {
                    onOriginalResolutionAvailable(srcW, srcH);
                }

                // Try to fetch distortion coefficients (varies by device)
                float[] lensDist = null;
                try {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                        // Brown model: k1, k2, p1, p2, k3 (and optionally higher order)
                        lensDist = cc.get(CameraCharacteristics.LENS_DISTORTION);
						if (lensDist != null) {
							Log.d(TAG, "Using CameraCharacteristics.LENS_DISTORTION, length=" + lensDist.length);
						}
                    }
                } catch (Exception e) {
                    Log.w(TAG, "LENS_DISTORTION unavailable: " + e.getMessage());
                }

                if (lensDist == null) {
                    try {
                        lensDist = cc.get(CameraCharacteristics.LENS_RADIAL_DISTORTION);
						if (lensDist != null) {
							Log.d(TAG, "Using CameraCharacteristics.LENS_RADIAL_DISTORTION, length=" + lensDist.length);
						}
                    } catch (Exception e) {
                        Log.w(TAG, "LENS_RADIAL_DISTORTION unavailable: " + e.getMessage());
                    }
                }

                if (lensDist != null && lensDist.length > 0) {
                    Log.d(TAG, "Distortion length=" + lensDist.length);
                    onDistortionAvailable(lensDist, lensDist.length);
                } else {
                    Log.d(TAG, "No distortion array available on this device");
                }

                // CRITICAL: Compute intrinsics adjusted for the output stream resolution (center crop + scale)
                // The raw intrinsics are for the full sensor (e.g., 1280x1280), but we're outputting
                // a different resolution (e.g., 1280x960). The principal point especially needs adjustment.
                Intr kStream = intrinsicsForStream(fx, fy, cx, cy, srcW, srcH, frameWidth, frameHeight);
                
                Log.d(TAG, "Original intrinsics: fx=" + fx + " fy=" + fy + " cx=" + cx + " cy=" + cy + " (for " + srcW + "x" + srcH + ")");
                Log.d(TAG, "Adjusted intrinsics: fx=" + kStream.fx + " fy=" + kStream.fy + " cx=" + kStream.cx + " cy=" + kStream.cy + " (for " + frameWidth + "x" + frameHeight + ")");
                
                // Send ADJUSTED intrinsics that match the output stream resolution
                onIntrinsicsAvailable(kStream.fx, kStream.fy, kStream.cx, kStream.cy, skew, frameWidth, frameHeight);

                onOriginalResolutionAvailable(srcW, srcH); // keep sending this if your native side logs it
                
                // Try to extract and send camera pose (CamInHmd) if available
                try {
                    float[] poseTranslation = cc.get(CameraCharacteristics.LENS_POSE_TRANSLATION);
                    float[] poseRotation = cc.get(CameraCharacteristics.LENS_POSE_ROTATION);
                    if (poseTranslation != null && poseTranslation.length >= 3 &&
                        poseRotation != null && poseRotation.length >= 4) {
                        // Pose translation is in meters, rotation is quaternion (x, y, z, w)
                        Log.d(TAG, "Camera pose found - Translation: [" + 
                              poseTranslation[0] + ", " + poseTranslation[1] + ", " + poseTranslation[2] + "]");
                        Log.d(TAG, "Camera pose found - Rotation: [" + 
                              poseRotation[0] + ", " + poseRotation[1] + ", " + poseRotation[2] + ", " + poseRotation[3] + "]");
                        onCameraPoseAvailable(
                            poseTranslation[0], poseTranslation[1], poseTranslation[2],
                            poseRotation[0], poseRotation[1], poseRotation[2], poseRotation[3]);
                    } else {
                        Log.d(TAG, "Camera pose not available from characteristics");
                    }
                } catch (Exception e) {
                    Log.w(TAG, "Failed to get camera pose: " + e.getMessage());
                }

            } catch (Exception e) {
                Log.w(TAG, "Failed to get intrinsics: "+e.getMessage());
            }

            

            // Setup ImageReader for camera frames
            Log.d(TAG, "Creating ImageReader " + frameWidth + "x" + frameHeight);
            imageReader = ImageReader.newInstance(frameWidth, frameHeight, 
                ImageFormat.YUV_420_888, 2);
            Log.d(TAG, "ImageReader created successfully");
                
            Log.d(TAG, "Setting up ImageReader listener...");
            imageReader.setOnImageAvailableListener(new ImageReader.OnImageAvailableListener() {
                @Override
                public void onImageAvailable(ImageReader reader) {
                    Image image = null;
                    try {
                        image = reader.acquireLatestImage();
                        if (image != null) {
                            processImage(image);
                        }
                    } catch (Exception e) {
                        Log.e(TAG, "Error processing image: " + e.getMessage());
                    } finally {
                        if (image != null) {
                            image.close();
                        }
                    }
                }
            }, backgroundHandler);
            Log.d(TAG, "ImageReader listener set");
            
            // Open camera
            Log.d(TAG, "Opening camera...");
            cameraManager.openCamera(cameraId, new CameraDevice.StateCallback() {
                @Override
                public void onOpened(CameraDevice camera) {
                    Log.d(TAG, "Camera opened");
                    cameraDevice = camera;
                    createCaptureSession();
                }
                
                @Override
                public void onDisconnected(CameraDevice camera) {
                    Log.w(TAG, "Camera disconnected");
                    camera.close();
                    cameraDevice = null;
                }
                
                @Override
                public void onError(CameraDevice camera, int error) {
                    Log.e(TAG, "Camera error: " + error);
                    camera.close();
                    cameraDevice = null;
                }
            }, backgroundHandler);
            
            Log.d(TAG, "Camera open request submitted");
            isCapturing = true;
            Log.d(TAG, "startCamera returning true");
            return true;
            
        } catch (Exception e) {
            Log.e(TAG, "Failed to start camera: " + e.getMessage());
            e.printStackTrace();
            return false;
        }
    }

    private static class Intr {
        float fx, fy, cx, cy;
    }

    private static android.graphics.Rect centerCrop(int srcW, int srcH, int dstW, int dstH) {
        float srcAspect = (float) srcW / (float) srcH;
        float dstAspect = (float) dstW / (float) dstH;
        if (dstAspect > srcAspect) {
            // Wider output: crop height
            int cropH = Math.round(srcW / dstAspect);
            int top = (srcH - cropH) / 2;
            return new android.graphics.Rect(0, top, srcW, top + cropH);
        } else {
            // Taller output: crop width
            int cropW = Math.round(srcH * dstAspect);
            int left = (srcW - cropW) / 2;
            return new android.graphics.Rect(left, 0, left + cropW, srcH);
        }
    }

    // Remember currently selected camera for dumps
    private String currentCameraId;
    // Remember if current camera is left (50) or right (51)
    private boolean currentIsLeftCamera;
    
    // Static preference for which camera to use (set before startCamera)
    // true = left camera (ID 50), false = right camera (ID 51)
    private static boolean preferLeftCamera = true;
    
    /**
     * Set the preferred camera before calling startCamera.
     * @param useLeft true for left camera (ID 50), false for right camera (ID 51)
     */
    public static void setPreferredCamera(boolean useLeft) {
        preferLeftCamera = useLeft;
        Log.d(TAG, "Camera preference set to: " + (useLeft ? "LEFT (50)" : "RIGHT (51)"));
    }
    
    /**
     * Get the current camera preference.
     * @return true if left camera is preferred
     */
    public static boolean getPreferredCamera() {
        return preferLeftCamera;
    }
	// Remember last saved dump file path
	private String lastCharacteristicsDumpPath;
	// Remember last JSON text
	private String lastCharacteristicsDumpJson;

    // Public method to dump all CameraCharacteristics to JSON and send to native
    public void dumpCameraCharacteristics() {
        try {
            if (cameraManager == null) {
                Log.e(TAG, "cameraManager is null; cannot dump characteristics");
                return;
            }
            String id = currentCameraId;
            if (id == null) {
                // Try a best-effort default
                String[] ids = cameraManager.getCameraIdList();
                if (ids != null && ids.length > 0) {
                    id = ids[0];
                }
            }
            if (id == null) {
                Log.e(TAG, "No cameraId available for dump");
                return;
            }

            CameraCharacteristics cc = cameraManager.getCameraCharacteristics(id);
            JSONObject root = new JSONObject();
            root.put("cameraId", id);
            root.put("sdk", Build.VERSION.SDK_INT);

            JSONObject values = new JSONObject();
            // Prefer official getKeys() when available
            boolean dumpedAny = false;
            try {
                Method getKeysMethod = CameraCharacteristics.class.getMethod("getKeys");
                @SuppressWarnings("unchecked")
                List<CameraCharacteristics.Key<?>> keys = (List<CameraCharacteristics.Key<?>>) getKeysMethod.invoke(cc);
                if (keys != null) {
                    for (CameraCharacteristics.Key<?> key : keys) {
                        String keyName = getKeyName(key);
                        Object val = safeGet(cc, key);
                        values.put(keyName, toJsonValue(val));
                        dumpedAny = true;
                    }
                }
            } catch (Throwable t) {
                Log.w(TAG, "getKeys() unavailable; will use reflection fallback: " + t.getMessage());
            }

            if (!dumpedAny) {
                // Reflection fallback: static fields of type CameraCharacteristics.Key
                for (Field f : CameraCharacteristics.class.getFields()) {
                    try {
                        if (CameraCharacteristics.Key.class.isAssignableFrom(f.getType())) {
                            @SuppressWarnings("unchecked")
                            CameraCharacteristics.Key<?> key = (CameraCharacteristics.Key<?>) f.get(null);
                            if (key != null) {
                                // Prefer the public constant field name for readability
                                String keyName = f.getName();
                                Object val = safeGet(cc, key);
                                values.put(keyName, toJsonValue(val));
                            }
                        }
                    } catch (Throwable ignored) { }
                }
            }

            root.put("values", values);
            String json = root.toString();
            Log.d(TAG, "Dumped CameraCharacteristics JSON length=" + json.length());
			lastCharacteristicsDumpJson = json;
			// Also persist the dump to a file accessible via adb (scoped storage safe)
			try {
				String fileName = "camera_characteristics_" + id + ".json";
				saveJsonToFile(fileName, json);
			} catch (Exception e) {
				Log.w(TAG, "Failed to save characteristics JSON to file: " + e.getMessage());
			}
            onCharacteristicsDumpAvailable(json);
        } catch (Exception e) {
            Log.e(TAG, "Failed to dump CameraCharacteristics: " + e.getMessage());
        }
    }

    private static Object safeGet(CameraCharacteristics cc, CameraCharacteristics.Key<?> key) {
        try {
            @SuppressWarnings("unchecked")
            Object val = cc.get((CameraCharacteristics.Key<Object>) key);
            return val;
        } catch (Throwable t) {
            return null;
        }
    }

    private static String getKeyName(CameraCharacteristics.Key<?> key) {
        try {
            Method m = key.getClass().getMethod("getName");
            m.setAccessible(true);
            Object name = m.invoke(key);
            if (name != null) return name.toString();
        } catch (Throwable ignored) { }
        // Fallback to toString()
        try {
            String s = key.toString();
            return s != null ? s : "<unknown>";
        } catch (Throwable t) {
            return "<unknown>";
        }
    }

    private static Object toJsonValue(Object val) {
        if (val == null) return JSONObject.NULL;
        if (val instanceof Number || val instanceof Boolean || val instanceof String) return val;
        if (val instanceof android.util.Size) {
            android.util.Size s = (android.util.Size) val;
            JSONObject o = new JSONObject();
            try { o.put("width", s.getWidth()); o.put("height", s.getHeight()); } catch (Exception ignored) {}
            return o;
        }
        if (val instanceof android.graphics.Rect) {
            android.graphics.Rect r = (android.graphics.Rect) val;
            JSONObject o = new JSONObject();
            try { o.put("left", r.left); o.put("top", r.top); o.put("right", r.right); o.put("bottom", r.bottom); } catch (Exception ignored) {}
            return o;
        }
        if (val instanceof android.util.Range) {
            android.util.Range<?> r = (android.util.Range<?>) val;
            JSONObject o = new JSONObject();
            try { o.put("lower", String.valueOf(r.getLower())); o.put("upper", String.valueOf(r.getUpper())); } catch (Exception ignored) {}
            return o;
        }
        if (val instanceof SizeF) {
            SizeF s = (SizeF) val;
            JSONObject o = new JSONObject();
            try { o.put("width", s.getWidth()); o.put("height", s.getHeight()); } catch (Exception ignored) {}
            return o;
        }
        if (val.getClass().isArray()) {
            JSONArray arr = new JSONArray();
            int len = java.lang.reflect.Array.getLength(val);
            for (int i = 0; i < len; i++) {
                Object e = java.lang.reflect.Array.get(val, i);
                arr.put(toJsonValue(e));
            }
            return arr;
        }
        if (val instanceof java.util.Collection) {
            JSONArray arr = new JSONArray();
            for (Object e : (java.util.Collection<?>) val) arr.put(toJsonValue(e));
            return arr;
        }
        // Fallback
        return String.valueOf(val);
    }

    private static Intr intrinsicsForStream(float fx, float fy, float cx, float cy,  int sensorW, int sensorH, int outW, int outH) {
        android.graphics.Rect crop = centerCrop(sensorW, sensorH, outW, outH);
        float sx = (float) outW / (float) crop.width();
        float sy = (float) outH / (float) crop.height();
        Intr k = new Intr();
        k.fx = fx * sx;
        k.fy = fy * sy;
        k.cx = (cx - crop.left) * sx;
        k.cy = (cy - crop.top) * sy;
        return k;
    }
	
	private void saveJsonToFile(String fileName, String jsonContent) {
		File baseDir = null;
		try {
			// Prefer Documents in the app-specific external files (no WRITE permission required)
			baseDir = context.getExternalFilesDir(Environment.DIRECTORY_DOCUMENTS);
			if (baseDir == null) {
				// Fallback to generic external files dir
				baseDir = context.getExternalFilesDir(null);
			}
			if (baseDir == null) {
				// Final fallback to internal files dir
				baseDir = context.getFilesDir();
			}
			File outDir = new File(baseDir, "Camera2");
			if (!outDir.exists()) {
				//noinspection ResultOfMethodCallIgnored
				outDir.mkdirs();
			}
			File outFile = new File(outDir, fileName);
			try (FileOutputStream fos = new FileOutputStream(outFile, false)) {
				byte[] bytes = jsonContent.getBytes(StandardCharsets.UTF_8);
				fos.write(bytes);
				fos.flush();
			}
			lastCharacteristicsDumpPath = outFile.getAbsolutePath();
			Log.i(TAG, "CameraCharacteristics JSON saved: " + outFile.getAbsolutePath());
		} catch (Throwable t) {
			Log.w(TAG, "Error saving JSON to file in " + (baseDir != null ? baseDir.getAbsolutePath() : "<null>") + ": " + t.getMessage());
		}
	}
	
	// Exposed getters/wrappers for native side
	public String getLastCharacteristicsDumpPath() {
		return lastCharacteristicsDumpPath != null ? lastCharacteristicsDumpPath : "";
	}
	public String getLastCharacteristicsDumpJson() {
		return lastCharacteristicsDumpJson != null ? lastCharacteristicsDumpJson : "";
	}
	
	public String dumpCameraCharacteristicsAndReturnPath() {
		dumpCameraCharacteristics();
		return getLastCharacteristicsDumpPath();
	}
	
	public String[] dumpCameraCharacteristicsAndReturnJsonAndPath() {
		dumpCameraCharacteristics();
		return new String[] { getLastCharacteristicsDumpJson(), getLastCharacteristicsDumpPath() };
	}
    
    private void createCaptureSession() {
        try {
            Surface surface = imageReader.getSurface();
            
            cameraDevice.createCaptureSession(Arrays.asList(surface),
                new CameraCaptureSession.StateCallback() {
                    @Override
                    public void onConfigured(CameraCaptureSession session) {
                        Log.d(TAG, "Capture session configured");
                        captureSession = session;
                        startCapture();
                    }
                    
                    @Override
                    public void onConfigureFailed(CameraCaptureSession session) {
                        Log.e(TAG, "Failed to configure capture session");
                    }
                }, backgroundHandler);
                
        } catch (Exception e) {
            Log.e(TAG, "Failed to create capture session: " + e.getMessage());
        }
    }
    
    private void startCapture() {
        try {
            CaptureRequest.Builder requestBuilder = 
                cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
            requestBuilder.addTarget(imageReader.getSurface());
            
            // Set auto-focus and auto-exposure
            requestBuilder.set(CaptureRequest.CONTROL_AF_MODE,
                CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE);
            requestBuilder.set(CaptureRequest.CONTROL_AE_MODE,
                CaptureRequest.CONTROL_AE_MODE_ON_AUTO_FLASH);
            
            captureSession.setRepeatingRequest(requestBuilder.build(),
                null, backgroundHandler);
                
            Log.d(TAG, "Camera capture started");
            
        } catch (Exception e) {
            Log.e(TAG, "Failed to start capture: " + e.getMessage());
        }
    }
    
    private void processImage(Image image) {
        try {
            // Get all planes (Y, U, V) for full color processing
            Image.Plane[] planes = image.getPlanes();
            if (planes.length >= 3) {
                Image.Plane yPlane = planes[0];  // Y (Luminance)
                Image.Plane uPlane = planes[1];  // U (Cb - Blue chroma)
                Image.Plane vPlane = planes[2];  // V (Cr - Red chroma)
                
                int imageWidth = image.getWidth();
                int imageHeight = image.getHeight();
                
                Log.v(TAG, "Full color processing: " + imageWidth + "x" + imageHeight + 
                      " planes=" + planes.length);
                
                // Extract Y data (full resolution)
                ByteBuffer yBuffer = yPlane.getBuffer();
                int ySize = yBuffer.remaining();
                byte[] yData = new byte[ySize];
                yBuffer.get(yData);
                
                // Extract U data (usually half resolution)
                ByteBuffer uBuffer = uPlane.getBuffer();
                int uSize = uBuffer.remaining();
                byte[] uData = new byte[uSize];
                uBuffer.get(uData);
                
                // Extract V data (usually half resolution)
                ByteBuffer vBuffer = vPlane.getBuffer();
                int vSize = vBuffer.remaining();
                byte[] vData = new byte[vSize];
                vBuffer.get(vData);
                
                Log.d(TAG, "YUV data sizes - Y: " + ySize + ", U: " + uSize + ", V: " + vSize);
                Log.d(TAG, "Plane strides - Y: " + yPlane.getRowStride() + 
                      ", U: " + uPlane.getRowStride() + ", V: " + vPlane.getRowStride());
                Log.d(TAG, "Pixel strides - Y: " + yPlane.getPixelStride() + 
                      ", U: " + uPlane.getPixelStride() + ", V: " + vPlane.getPixelStride());
                
                // Convert YUV to RGB
                byte[] rgbaData = convertYuvToRgba(yData, uData, vData, imageWidth, imageHeight,
                    yPlane.getRowStride(), uPlane.getRowStride(), vPlane.getRowStride(),
                    yPlane.getPixelStride(), uPlane.getPixelStride(), vPlane.getPixelStride());
                
                // Send to native
                if (rgbaData != null) {
                    latestFrameData = rgbaData;
                    Log.v(TAG, "Sending full color RGBA data to native: size=" + rgbaData.length);
                    onFrameAvailable(rgbaData, frameWidth, frameHeight);
                }
            } else {
                Log.w(TAG, "Not enough planes for color processing (got " + planes.length + "), falling back to grayscale");
                // Fallback to grayscale processing if not enough planes
                processImageGrayscale(image);
            }
        } catch (Exception e) {
            Log.e(TAG, "Error in processImage: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    // Fallback method for grayscale processing
    private void processImageGrayscale(Image image) {
        try {
            Image.Plane[] planes = image.getPlanes();
            if (planes.length > 0) {
                Image.Plane yPlane = planes[0];
                ByteBuffer yBuffer = yPlane.getBuffer();
                
                int imageWidth = image.getWidth();
                int imageHeight = image.getHeight();
                
                // Extract Y data
                byte[] yData = new byte[frameWidth * frameHeight];
                int pixelStride = yPlane.getPixelStride();
                int rowStride = yPlane.getRowStride();
                
                if (pixelStride == 1 && rowStride == imageWidth) {
                    yBuffer.get(yData);
                } else {
                    byte[] rowData = new byte[rowStride];
                    for (int row = 0; row < Math.min(imageHeight, frameHeight); row++) {
                        if (yBuffer.remaining() >= rowStride) {
                            yBuffer.get(rowData, 0, rowStride);
                            for (int col = 0; col < Math.min(imageWidth, frameWidth); col += pixelStride) {
                                yData[row * frameWidth + col] = rowData[col];
                            }
                        }
                    }
                }
                
                // Convert to grayscale RGBA
                byte[] rgbaData = convertGrayscaleToRgba(yData, frameWidth, frameHeight);
                
                if (rgbaData != null) {
                    latestFrameData = rgbaData;
                    onFrameAvailable(rgbaData, frameWidth, frameHeight);
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Error in grayscale processing: " + e.getMessage());
        }
    }
    
    // New full-color YUV to RGBA conversion
    private byte[] convertYuvToRgba(byte[] yData, byte[] uData, byte[] vData, 
                                   int width, int height,
                                   int yRowStride, int uRowStride, int vRowStride,
                                   int yPixelStride, int uPixelStride, int vPixelStride) {
        
        byte[] rgba = new byte[frameWidth * frameHeight * 4];
        
        try {
            // YUV420 typically has U and V planes at half resolution
            int uvHeight = height / 2;
            int uvWidth = width / 2;
            
            Log.d(TAG, "YUV conversion - Full: " + width + "x" + height + 
                  ", UV: " + uvWidth + "x" + uvHeight);
            
            for (int row = 0; row < frameHeight && row < height; row++) {
                for (int col = 0; col < frameWidth && col < width; col++) {
                    
                    // Get Y value (full resolution)
                    int yIndex = row * yRowStride + col * yPixelStride;
                    int yValue = (yIndex < yData.length) ? (yData[yIndex] & 0xFF) : 128;
                    
                    // Get U and V values (half resolution, so divide by 2)
                    int uvRow = row / 2;
                    int uvCol = col / 2;
                    int uIndex = uvRow * uRowStride + uvCol * uPixelStride;
                    int vIndex = uvRow * vRowStride + uvCol * vPixelStride;
                    
                    int uValue = (uIndex < uData.length) ? (uData[uIndex] & 0xFF) : 128;
                    int vValue = (vIndex < vData.length) ? (vData[vIndex] & 0xFF) : 128;
                    
                    // YUV to RGB conversion - try full range (0-255) first
                    // Many modern cameras use full range YUV instead of limited range
                    int y = yValue;
                    int u = uValue - 128;
                    int v = vValue - 128;
                    
                    // Apply conversion matrix (full range version)
                    int r = (int)(y + 1.402f * v);
                    int g = (int)(y - 0.344f * u - 0.714f * v);
                    int b = (int)(y + 1.772f * u);
                    
                    // Clamp to 0-255 range
                    r = Math.max(0, Math.min(255, r));
                    g = Math.max(0, Math.min(255, g));
                    b = Math.max(0, Math.min(255, b));
                    
                    // Set BGRA format for UE5
                    int pixelIndex = (row * frameWidth + col) * 4;
                    rgba[pixelIndex] = (byte)b;       // B
                    rgba[pixelIndex + 1] = (byte)g;   // G
                    rgba[pixelIndex + 2] = (byte)r;   // R
                    rgba[pixelIndex + 3] = (byte)255; // A
                }
            }
            
            // Log first few RGB values for debugging
            Log.d(TAG, "RGB conversion sample - R:" + (rgba[2] & 0xFF) + 
                  " G:" + (rgba[1] & 0xFF) + " B:" + (rgba[0] & 0xFF));
            
            return rgba;
            
        } catch (Exception e) {
            Log.e(TAG, "Error in YUV to RGBA conversion: " + e.getMessage());
            return convertGrayscaleToRgba(yData, frameWidth, frameHeight);
        }
    }
    
    // Legacy grayscale conversion method (renamed)
    private byte[] convertGrayscaleToRgba(byte[] yuv, int width, int height) {
        byte[] rgba = new byte[width * height * 4];
        
        // Check if YUV data is all zeros (debugging)
        boolean allZero = true;
        for (int i = 0; i < Math.min(yuv.length, 100); i++) {
            if (yuv[i] != 0) {
                allZero = false;
                break;
            }
        }
        
        if (allZero) {
            Log.w(TAG, "YUV data appears to be all zeros, generating test pattern");
            // Generate bright colorful test pattern - quarters with different colors
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int i = (y * width + x) * 4;
                    
                    // Create 4 colorful quarters
                    int red, green, blue;
                    if (x < width/2 && y < height/2) {
                        // Top-left: Red
                        red = 255; green = 0; blue = 0;
                    } else if (x >= width/2 && y < height/2) {
                        // Top-right: Green  
                        red = 0; green = 255; blue = 0;
                    } else if (x < width/2 && y >= height/2) {
                        // Bottom-left: Blue
                        red = 0; green = 0; blue = 255;
                    } else {
                        // Bottom-right: Yellow
                        red = 255; green = 255; blue = 0;
                    }
                    
                    // Apply to BGRA format (UE5)
                    rgba[i] = (byte)blue;      // B
                    rgba[i + 1] = (byte)green; // G
                    rgba[i + 2] = (byte)red;   // R
                    rgba[i + 3] = (byte)255;   // A (fully opaque)
                }
            }
            
            // Log first few RGBA values to verify pattern generation
            Log.d(TAG, "Test pattern generated - first RGBA values: [" +
                  (rgba[0] & 0xFF) + "," + (rgba[1] & 0xFF) + "," + 
                  (rgba[2] & 0xFF) + "," + (rgba[3] & 0xFF) + "] [" +
                  (rgba[4] & 0xFF) + "," + (rgba[5] & 0xFF) + "," + 
                  (rgba[6] & 0xFF) + "," + (rgba[7] & 0xFF) + "]");
        } else {
            Log.d(TAG, "YUV data has values, converting to grayscale");
            // Convert Y channel to RGBA (grayscale)
            for (int i = 0; i < width * height; i++) {
                int y = yuv[i] & 0xFF;
                
                rgba[i * 4] = (byte)y;       // B = Y
                rgba[i * 4 + 1] = (byte)y;   // G = Y  
                rgba[i * 4 + 2] = (byte)y;   // R = Y
                rgba[i * 4 + 3] = (byte)255; // A = 255
            }
        }
        
        return rgba;
    }
    
    public void stopCamera() {
        isCapturing = false;
        
        if (captureSession != null) {
            captureSession.close();
            captureSession = null;
        }
        
        if (cameraDevice != null) {
            cameraDevice.close();
            cameraDevice = null;
        }
        
        if (imageReader != null) {
            imageReader.close();
            imageReader = null;
        }
        
        stopBackgroundThread();
        Log.d(TAG, "Camera stopped");
    }
    
    private void startBackgroundThread() {
        backgroundThread = new HandlerThread("CameraBackground");
        backgroundThread.start();
        backgroundHandler = new Handler(backgroundThread.getLooper());
    }
    
    private void stopBackgroundThread() {
        if (backgroundThread != null) {
            backgroundThread.quitSafely();
            try {
                backgroundThread.join();
                backgroundThread = null;
                backgroundHandler = null;
            } catch (InterruptedException e) {
                Log.e(TAG, "Error stopping background thread");
            }
        }
    }
    
    public byte[] getLatestFrame() {
        return latestFrameData;
    }
    
    // Method to check permission status (callable from C++)
    public boolean hasCameraPermission() {
        return checkCameraPermission();
    }
    
    // Method to request permission (callable from C++)
    public void requestPermission() {
        requestCameraPermission();
    }
}