import com.forklift.companion.PcmWav;
import java.io.File;
public class DecodeWav {
 public static void main(String[] args)throws Exception { System.out.println(PcmWav.prepare(new File(args[0]),new File(args[1])).getAbsolutePath()); }
}
