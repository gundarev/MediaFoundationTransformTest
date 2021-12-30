# MediaFoundationTransformTest

A code snippet showing how to deal with a hardware Media Foundation H.264 encoder.

Originally taken from a [gist](https://gist.github.com/KeloCube/0e56ba7f2c5729223483147eb35d9cc7) from [this StackOverflow question](https://stackoverflow.com/q/61945544/868014).

Below is an email from my comment explaining what this code does:

> I converted to full project and normal repository here https://github.com/roman380/MediaFoundationTransformTest
> 
> The code overall looks good, maybe just makes some unobvious assumptions (see below).
> 
> This gist does not do enumeration per se, it just picks a very specific one and tests it out. If you want enumeration code, I created a branch "mftenum" and uploaded code from my website and also another sample project, see https://github.com/roman380/MediaFoundationTransformTest/tree/mftenum/extra
> 
> The gist you linked does the following:
> 
> uses MFTEnum to enumerate only hardware H.264 video encoder MFTs, just those
> the following code assumes MFT is async (which is true for all hardware encoders), so this excludes software H.264 encoder from Microsoft
> D3D11 device is created with defaults, and the hardware MFT to work needs a matching device, device sitting on the same hardware, so the test would only work with a matching MFT
> 
> You will see all this in inline comments in this commit https://github.com/roman380/MediaFoundationTransformTest/commit/0ba940a2c7f948e9f9db8e16015e9b6c960cd8ed

See also:

- [https://stackoverflow.com/search?q=hardware+H.264+%5Bms-media-foundation%5D](https://stackoverflow.com/search?q=hardware+H.264+%5Bms-media-foundation%5D)
