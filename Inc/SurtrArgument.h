#ifndef SURTUR_ARGUMENT_H
#define SURTUR_ARGUMENT_H

constexpr UINT MIN_SUB_DIVIDE_COUNT = 7u;
constexpr UINT MAX_SUB_DIVIDE_COUNT = 9u;

struct SurtrArgument
{
    UINT SubDivideCount;
    UINT ShadowMapSize;
    BOOL FullScreenMode;
    UINT Width;
    UINT Height;

    explicit SurtrArgument(
        UINT subDivideCount = 8u,
        UINT shadowMapSize = 8192u,
        BOOL fullScreenMode = FALSE,
        UINT width = GetSystemMetrics(SM_CXSCREEN),
        UINT height = GetSystemMetrics(SM_CYSCREEN))
        : SubDivideCount(subDivideCount), ShadowMapSize(shadowMapSize), FullScreenMode(fullScreenMode), Width(width), Height(height) {}
};

SurtrArgument CollectSurtrArgument()
{
    SurtrArgument arguments;

    int nArgs;
    LPWSTR* szArgList = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (szArgList == nullptr)
        return arguments;

    if (nArgs >= 2)
    {
        arguments.SubDivideCount = std::min(MAX_SUB_DIVIDE_COUNT, std::max(MIN_SUB_DIVIDE_COUNT, static_cast<UINT>(std::stoi(szArgList[1]))));
    }
    if (nArgs >= 3)
    {
        arguments.ShadowMapSize = std::min(8192u, std::max(4096u, static_cast<UINT>(std::stoi(szArgList[2]))));
    }
    if (nArgs >= 4)
    {
        arguments.FullScreenMode = std::stoi(szArgList[3]);
    }
    if (nArgs >= 6 && !arguments.FullScreenMode)
    {
        arguments.Width = std::max(1280u, static_cast<UINT>(std::stoi(szArgList[4])));
        arguments.Height = std::max(720u, static_cast<UINT>(std::stoi(szArgList[5])));
    }

    if (szArgList != nullptr)
        LocalFree(szArgList);

    return arguments;
}

#endif