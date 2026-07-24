using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;

internal static class ExtractRiscosSprite
{
    private static int Main(string[] args)
    {
        if (args.Length < 2 || args.Length > 3)
        {
            Console.Error.WriteLine(
                "Usage: ExtractRiscosSprite <source-sprite> <output.png> " +
                "[--transparent-border]"
            );
            return 1;
        }

        try
        {
            using (FileStream stream = File.OpenRead(args[0]))
            using (BinaryReader reader = new BinaryReader(stream))
            {
                int spriteCount = reader.ReadInt32();
                int firstSprite = reader.ReadInt32();

                if (spriteCount < 1)
                {
                    throw new InvalidDataException("The sprite file is empty.");
                }
                stream.Position = firstSprite - 4;
                int spriteStart = checked((int) stream.Position);
                reader.ReadInt32();
                reader.ReadBytes(12);
                int wordsMinusOne = reader.ReadInt32();
                int rowsMinusOne = reader.ReadInt32();
                int leftBit = reader.ReadInt32();
                int rightBit = reader.ReadInt32();
                int imageOffset = reader.ReadInt32();
                reader.ReadInt32();
                uint mode = reader.ReadUInt32();
                int type = (int) ((mode >> 27) & 15);

                if (type != 6 || leftBit != 0 || rightBit != 31)
                {
                    throw new InvalidDataException(
                        "Only word-aligned 32-bpp sprites are supported."
                    );
                }
                int width = wordsMinusOne + 1;
                int height = rowsMinusOne + 1;
                stream.Position = spriteStart + imageOffset;
                using (Bitmap bitmap = new Bitmap(
                    width,
                    height,
                    PixelFormat.Format32bppArgb
                ))
                {
                    for (int y = 0; y < height; ++y)
                    {
                        for (int x = 0; x < width; ++x)
                        {
                            byte red = reader.ReadByte();
                            byte green = reader.ReadByte();
                            byte blue = reader.ReadByte();
                            reader.ReadByte();
                            int alpha = 255;

                            if (args.Length == 3 &&
                                args[2] == "--transparent-border")
                            {
                                int difference = Math.Max(
                                    Math.Max(
                                        Math.Abs(red - 238),
                                        Math.Abs(green - 238)
                                    ),
                                    Math.Abs(blue)
                                );
                                if (difference <= 24)
                                {
                                    alpha = 0;
                                }
                            }
                            bitmap.SetPixel(x, y, Color.FromArgb(
                                alpha,
                                red,
                                green,
                                blue
                            ));
                        }
                    }
                    bitmap.Save(args[1], ImageFormat.Png);
                }
            }
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error.Message);
            return 1;
        }
    }
}
