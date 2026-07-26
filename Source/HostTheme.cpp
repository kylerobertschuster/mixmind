#include "HostTheme.h"

namespace HostTheme
{

Host detect()
{
    juce::PluginHostType host;

    if (host.isAbletonLive())   return Host::AbletonLive;
    if (host.isLogic())         return Host::LogicPro;
    if (host.isGarageBand())    return Host::GarageBand;
    if (host.isProTools())      return Host::ProTools;
    if (host.isReaper())        return Host::Reaper;
    if (host.isFruityLoops())   return Host::FLStudio;
    if (host.isBitwigStudio())    return Host::Bitwig;
    if (host.isCubase())        return Host::Cubase;
    if (host.isStudioOne())     return Host::StudioOne;

    return Host::Unknown;
}

FontSet getFonts()
{
    switch (detect())
    {
        case Host::AbletonLive:
            return { "SF Pro Display", "SF Mono", "SF Pro Display" };

        case Host::LogicPro:
        case Host::GarageBand:
            return { "SF Pro Display", "SF Mono", "SF Pro Display" };

        case Host::ProTools:
            return { "Helvetica Neue", "Menlo", "Helvetica Neue" };

        case Host::Reaper:
            return { "Inter", "JetBrains Mono", "Inter" };

        case Host::FLStudio:
            return { "Inter", "JetBrains Mono", "Inter" };

        case Host::StudioOne:
            return { "Inter", "JetBrains Mono", "Inter" };

        case Host::Bitwig:
            return { "SF Pro Display", "SF Mono", "SF Pro Display" };

        case Host::Cubase:
            return { "Inter", "JetBrains Mono", "Inter" };

        case Host::Unknown:
        default:
            return { "SF Pro Display", "SF Mono", "SF Pro Display" };
    }
}

ColorSet getColors()
{
    ColorSet cs;
    cs.bg      = juce::Colour (0xff0f1012);
    cs.surface = juce::Colour (0xff16171b);
    cs.border  = juce::Colour (0xff25272d);
    cs.text    = juce::Colour (0xffd0d0d0);
    cs.muted   = juce::Colour (0xff6b6f7a);
    cs.accent  = accentForHost (detect());
    return cs;
}

juce::Colour accentForHost (Host host)
{
    switch (host)
    {
        case Host::AbletonLive:  return juce::Colour (0xfff0a030);
        case Host::LogicPro:
        case Host::GarageBand:   return juce::Colour (0xff5b9bd5);
        case Host::ProTools:     return juce::Colour (0xff3b82f6);
        case Host::Reaper:       return juce::Colour (0xff22c55e);
        case Host::FLStudio:     return juce::Colour (0xfff97316);
        case Host::StudioOne:    return juce::Colour (0xff06b6d4);
        case Host::Bitwig:       return juce::Colour (0xffec4899);
        case Host::Cubase:       return juce::Colour (0xff3b82f6);
        case Host::Unknown:
        default:                 return juce::Colour (0xffa855f7);
    }
}

} // namespace HostTheme
