using System;
using System.IO;
using System.Linq;
using LibOrbisPkg.PFS;

class ExtentRegression {
    static MemoryStream Build(int count, bool signed) {
        var root=new FSDir();
        for(int i=0;i<count;++i)
            root.Files.Add(new FSFile(s=>{},i.ToString("X8")+".asset",0){Parent=root});
        var image=new MemoryStream();
        var builder=new PfsBuilder(new PfsProperties{root=root,BlockSize=65536,FileTime=1,
            Sign=signed,Encrypt=false,Seed=new byte[16],EKPFS=new byte[32]});
        image.SetLength(builder.CalculatePfsSize()); // production PKG writer preallocates its PFS image
        builder.WriteImage(image);
        return image;
    }
    static PfsReader Read(MemoryStream image) {
        return new PfsReader(new LibOrbisPkg.Util.StreamReader(image));
    }
    static void Main() {
        using(var image=Build(10000,false)) {
            var reader=Read(image);
            if(reader.GetAllFiles().Count()!=10000) throw new Exception("Large catalog lost files: "+reader.GetAllFiles().Count());
            var table=reader.GetSuperRoot().children.OfType<PfsReader.File>().Single(f=>f.name=="flat_path_table");
            if(table.size<=65536) throw new Exception("Fixture did not exercise multiple explicit blocks");
            const int perBlock=65536/0xA8;
            long inodeOffset=65536+(table.ino/perBlock)*65536+(table.ino%perBlock)*0xA8;
            image.Position=inodeOffset+0x68; // D32 direct block 1, after the initial start block
            image.Write(BitConverter.GetBytes(-1337),0,4);
            bool rejected=false;
            try{Read(image);}catch(Exception e){rejected=e.Message.Contains("Noncontiguous unsigned");}
            if(!rejected) throw new Exception("Fragmented/corrupt explicit extent was accepted");
        }
        using(var image=Build(3,true))
            if(Read(image).GetAllFiles().Count()!=3) throw new Exception("Signed image regression");
        Console.WriteLine("PASS: 10000-file unsigned catalog; corrupt extent rejected; signed image unchanged");
    }
}
