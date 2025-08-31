package dummy;

public class StaticArgTarget {
    public static int addInts(int a, int b) {
        return a + b;
    }

    public static String concatStrings(String a, String b) {
        return a + b;
    }

    public static int sumIntArray(int[] arr) {
        int sum = 0;
        for (int v : arr) {
            sum += v;
        }
        return sum;
    }

    public static Target passThrough(Target t) {
        return t;
    }
}
