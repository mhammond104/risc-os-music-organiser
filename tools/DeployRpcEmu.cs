using System;
using System.IO;
using System.Security.Cryptography;
using System.Windows.Forms;

internal static class DeployRpcEmu
{
    private const string RepositoryPath =
        @"D:\src\risc-os-image-organiser";

    private const string DestinationDirectory =
        @"D:\src\RPCEmu - Direct\hostfs\Martin's Apps\!Focal";

    private static readonly string[,] Files =
    {
        { @"build\riscos\RunImage.aif", "!RunImage,ff8" },
        { @"app\!Focal\!Boot", "!Boot,feb" },
        { @"app\!Focal\!Run", "!Run,feb" },
        { @"app\!Focal\!Sprites", "!Sprites,ff9" },
        { @"app\!Focal\!Sprites22", "!Sprites22,ff9" },
        { @"app\!Focal\!Sprites11", "!Sprites11,ff9" },
        { @"app\!Focal\Messages", "Messages,fff" },
        { @"app\!Focal\README", "README,fff" }
    };

    [STAThread]
    private static int Main(string[] args)
    {
        bool quiet = args.Length > 0 &&
            string.Equals(args[0], "--quiet", StringComparison.OrdinalIgnoreCase);

        try
        {
            Deploy();

            if (!quiet)
            {
                MessageBox.Show(
                    "The Focal application was copied to RPCEmu HostFS successfully.",
                    "Focal deployment",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information
                );
            }

            return 0;
        }
        catch (Exception error)
        {
            if (!quiet)
            {
                MessageBox.Show(
                    error.Message,
                    "Focal deployment failed",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error
                );
            }

            return 1;
        }
    }

    private static void Deploy()
    {
        if (string.IsNullOrEmpty(DestinationDirectory))
        {
            throw new InvalidOperationException("The RPCEmu destination is invalid.");
        }

        Directory.CreateDirectory(DestinationDirectory);

        for (int index = 0; index < Files.GetLength(0); ++index)
        {
            string sourcePath = Path.Combine(RepositoryPath, Files[index, 0]);
            string destinationPath = Path.Combine(
                DestinationDirectory,
                Files[index, 1]
            );

            if (!File.Exists(sourcePath))
            {
                throw new FileNotFoundException(
                    "A deployment resource was not found. Rebuild the application first.",
                    sourcePath
                );
            }

            File.Copy(sourcePath, destinationPath, true);
            if (!FilesMatch(sourcePath, destinationPath))
            {
                throw new IOException(
                    "A copied file failed verification: " + Files[index, 1]
                );
            }
        }
    }

    private static bool FilesMatch(string firstPath, string secondPath)
    {
        byte[] firstHash;
        byte[] secondHash;

        using (SHA256 sha256 = SHA256.Create())
        using (FileStream first = File.OpenRead(firstPath))
        {
            firstHash = sha256.ComputeHash(first);
        }

        using (SHA256 sha256 = SHA256.Create())
        using (FileStream second = File.OpenRead(secondPath))
        {
            secondHash = sha256.ComputeHash(second);
        }

        if (firstHash.Length != secondHash.Length)
        {
            return false;
        }

        for (int index = 0; index < firstHash.Length; ++index)
        {
            if (firstHash[index] != secondHash[index])
            {
                return false;
            }
        }

        return true;
    }
}
