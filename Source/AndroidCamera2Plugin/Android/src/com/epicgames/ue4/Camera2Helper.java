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
import android.util.Log;
import android.view.Surface;
import java.nio.ByteBuffer;
import java.util.Arrays;
import android.Manifest;

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
    private int frameWidth = 800;
    private int frameHeight = 600;
    private boolean isCapturing = false;
    
    // Native callback
    private static native void onFrameAvailable(byte[] data, int width, int height);
    
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
            
            // First pass: look for Quest 3 special cameras (50, 51)
            for (String id : cameraIds) {
                if (id.equals("50") || id.equals("51")) {
                    Log.d(TAG, "=== Camera ID: " + id + " (PRIORITY) ===");
                    try {
                        CameraCharacteristics characteristics = cameraManager.getCameraCharacteristics(id);
                        Integer facing = characteristics.get(CameraCharacteristics.LENS_FACING);
                        String facingStr = "UNKNOWN";
                        if (facing != null) {
                            if (facing == CameraCharacteristics.LENS_FACING_FRONT) facingStr = "FRONT";
                            else if (facing == CameraCharacteristics.LENS_FACING_BACK) facingStr = "BACK";
                            else if (facing == CameraCharacteristics.LENS_FACING_EXTERNAL) facingStr = "EXTERNAL";
                        }
                        Log.d(TAG, "Priority Camera " + id + " facing: " + facingStr + " (value=" + facing + ")");
                        
                        cameraId = id;
                        Log.d(TAG, "Selected Quest 3 special camera ID: " + id + " (" + facingStr + ")");
                        break; // Use first special camera found
                    } catch (Exception e) {
                        Log.w(TAG, "Could not get characteristics for priority camera " + id + ": " + e.getMessage());
                    }
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