using System;
using System.IO;
using System.Security.Cryptography;
using System.Windows.Forms;

internal static class DeployRpcEmu
{
    private const string SourcePath =
        @"D:\src\risc-os-image-organiser\build\riscos\RunImage.aif";

    private const string DestinationPath =
        @"D:\src\RPCEmu - Direct\hostfs\src\!ImgOrg\!RunImage,ff8";

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
                    "RunImage.aif was copied to RPCEmu HostFS successfully.",
                    "Image Organiser deployment",
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
                    "Image Organiser deployment failed",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error
                );
            }

            return 1;
        }
    }

    private static void Deploy()
    {
        string destinationDirectory = Path.GetDirectoryName(DestinationPath);

        if (!File.Exists(SourcePath))
        {
            throw new FileNotFoundException(
                "Build output was not found. Build RunImage.aif first.",
                SourcePath
            );
        }

        if (string.IsNullOrEmpty(destinationDirectory))
        {
            throw new InvalidOperationException("The RPCEmu destination is invalid.");
        }

        Directory.CreateDirectory(destinationDirectory);
        File.Copy(SourcePath, DestinationPath, true);

        if (!FilesMatch(SourcePath, DestinationPath))
        {
            throw new IOException("The copied file failed verification.");
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
