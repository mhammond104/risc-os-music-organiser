using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Text;

internal static class BuildRiscosSprites
{
    private const string SpriteName = "!imgorg";

    private static int Main(string[] args)
    {
        if (args.Length != 2)
        {
            Console.Error.WriteLine(
                "Usage: BuildRiscosSprites <source.png> <application-directory>"
            );
            return 1;
        }

        try
        {
            Directory.CreateDirectory(args[1]);
            using (Bitmap source = new Bitmap(args[0]))
            {
                WriteSprite(source, Path.Combine(args[1], "!Sprites"), 34, 17, 90, 45);
                WriteSprite(source, Path.Combine(args[1], "!Sprites22"), 34, 34, 90, 90);
                WriteSprite(source, Path.Combine(args[1], "!Sprites11"), 68, 68, 180, 180);
            }
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error.Message);
            return 1;
        }
    }

    private static void WriteSprite(
        Bitmap source,
        string outputPath,
        int width,
        int height,
        int xDpi,
        int yDpi
    )
    {
        using (Bitmap resized = Resize(source, width, height))
        using (FileStream stream = File.Create(outputPath))
        using (BinaryWriter writer = new BinaryWriter(stream))
        {
            int imageBytes = width * height * 4;
            int spriteBytes = 44 + imageBytes;

            writer.Write(1);
            writer.Write(16);
            writer.Write(16 + spriteBytes);

            writer.Write(spriteBytes);
            WriteName(writer, SpriteName);
            writer.Write(width - 1);
            writer.Write(height - 1);
            writer.Write(0);
            writer.Write(31);
            writer.Write(44);
            writer.Write(44);
            writer.Write(NewStyleMode(xDpi, yDpi));

            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    Color pixel = resized.GetPixel(x, y);
                    writer.Write(pixel.R);
                    writer.Write(pixel.G);
                    writer.Write(pixel.B);
                    writer.Write((byte) 0);
                }
            }
        }
    }

    private static Bitmap Resize(Bitmap source, int width, int height)
    {
        Bitmap result = new Bitmap(width, height, PixelFormat.Format32bppArgb);
        using (Graphics graphics = Graphics.FromImage(result))
        {
            graphics.Clear(Color.Black);
            graphics.CompositingMode = CompositingMode.SourceCopy;
            graphics.CompositingQuality = CompositingQuality.HighQuality;
            graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
            graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
            graphics.DrawImage(source, 0, 0, width, height);
        }
        return result;
    }

    private static void WriteName(BinaryWriter writer, string name)
    {
        byte[] bytes = Encoding.ASCII.GetBytes(name);
        byte[] field = new byte[12];
        Array.Copy(bytes, field, Math.Min(bytes.Length, field.Length));
        writer.Write(field);
    }

    private static uint NewStyleMode(int xDpi, int yDpi)
    {
        return 1u |
            ((uint) xDpi << 1) |
            ((uint) yDpi << 14) |
            (6u << 27);
    }
}
