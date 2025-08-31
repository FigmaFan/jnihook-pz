package dummy;

public class HookTests {
    private static native boolean run();

    public static void main(String[] args) {
        if (!run()) {
            throw new AssertionError("JNIHook tests failed");
        }
    }

    static {
        System.loadLibrary("hooktest");
    }
}
