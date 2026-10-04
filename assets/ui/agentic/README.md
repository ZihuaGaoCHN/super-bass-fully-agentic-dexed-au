# Agentic Dexed UI typography

The pixel interface uses geometric eight-pixel spacing and hard-edged native JUCE drawing. The bundled Noto Sans family remains the readable UI typeface and provides broad fallback coverage for conversation text, CJK, and symbols.

Silkscreen is the intended short-label display face (Google Fonts, OFL-1.1). Its binary is deliberately not duplicated in this source tree until the release asset audit can pin and checksum an upstream font revision; the theme falls back to the already bundled Noto Sans resource without changing layout metrics.
