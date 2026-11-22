package com.lscardinali.unrealtournament.setup

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Unreal Tournament '99 look for the app's own screens: brushed gunmetal, bevelled steel plates with rivets, the
 * bronze-gold of the logo for highlights and focus, a touch of red. Regular (modern) type.
 */
object UT {
    val Gold = Color(0xFFE3B250)
    val GoldLight = Color(0xFFF7DD8C)
    val GoldDark = Color(0xFF9C6B1F)
    val Red = Color(0xFFB3261E)
    val Text = Color(0xFFE6E2D8)
    val TextDim = Color(0xFF9A968D)
    val Steel = Color(0xFF23262B)
    val SteelDark = Color(0xFF16181C)
    val EdgeLight = Color(0xFF4B5058)
    val EdgeDark = Color(0xFF07080A)

    val bronze = Brush.verticalGradient(listOf(GoldLight, Gold, GoldDark, Gold))
}

/** The brushed-metal backdrop. */
@Composable
fun MetalBackground(content: @Composable BoxScope.() -> Unit) {
    Box(
        Modifier
            .fillMaxSize()
            .background(Brush.verticalGradient(listOf(Color(0xFF1D2024), Color(0xFF0B0C0E))))
            .drawBehind {
                var y = 0f
                val step = 3.dp.toPx()
                while (y < size.height) {
                    drawLine(Color.White.copy(alpha = if ((y / step).toInt() % 3 == 0) 0.025f else 0.012f),
                             Offset(0f, y), Offset(size.width, y), 1f)
                    y += step
                }
                drawRect(Brush.radialGradient(listOf(Color.Transparent, Color.Black.copy(alpha = 0.55f)),
                                              center = center, radius = size.maxDimension * 0.75f))
            },
        content = content,
    )
}

private fun DrawScope.bevel(light: Color, dark: Color, w: Float) {
    drawRect(light, Offset.Zero, Size(size.width, w))
    drawRect(light, Offset.Zero, Size(w, size.height))
    drawRect(dark, Offset(0f, size.height - w), Size(size.width, w))
    drawRect(dark, Offset(size.width - w, 0f), Size(w, size.height))
}

private fun DrawScope.rivet(c: Offset, r: Float) {
    drawCircle(Brush.radialGradient(listOf(Color(0xFF9AA0A8), Color(0xFF3A3E44), Color(0xFF15171A)),
                                    center = c - Offset(r * 0.3f, r * 0.3f), radius = r * 1.4f), r, c)
}

/** A bevelled steel plate with rivets in the corners. */
@Composable
fun SteelPanel(modifier: Modifier = Modifier, title: String? = null, content: @Composable ColumnScope.() -> Unit) {
    Column(
        modifier
            .background(Brush.verticalGradient(listOf(UT.Steel, UT.SteelDark)))
            .drawBehind {
                bevel(UT.EdgeLight, UT.EdgeDark, 2.dp.toPx())
                val i = 9.dp.toPx()
                val r = 3.dp.toPx()
                for (c in listOf(Offset(i, i), Offset(size.width - i, i), Offset(i, size.height - i),
                                 Offset(size.width - i, size.height - i))) rivet(c, r)
            }
            .padding(horizontal = 22.dp, vertical = 16.dp),
    ) {
        if (title != null) {
            Text(title.uppercase(), color = UT.Gold, fontSize = 13.sp, fontWeight = FontWeight.Bold,
                 letterSpacing = 3.sp)
            Box(Modifier.padding(top = 5.dp, bottom = 10.dp).fillMaxWidth().height(1.dp)
                    .background(Brush.horizontalGradient(listOf(UT.Gold.copy(alpha = 0.7f), Color.Transparent))))
        }
        content()
    }
}

/** The game's name, logo style: bronze UNREAL over spaced TOURNAMENT. */
@Composable
fun UtTitle(modifier: Modifier = Modifier) {
    Column(modifier) {
        Text("UNREAL", style = TextStyle(brush = UT.bronze, fontSize = 52.sp, fontWeight = FontWeight.Black,
                                         letterSpacing = 6.sp))
        Text("TOURNAMENT", color = Color(0xFFB9BEC6), fontSize = 18.sp, fontWeight = FontWeight.Bold,
             letterSpacing = 10.sp, modifier = Modifier.padding(start = 4.dp))
        Box(Modifier.padding(top = 14.dp).width(250.dp).height(3.dp)
                .background(Brush.horizontalGradient(listOf(UT.Red, UT.Red.copy(alpha = 0f)))))
    }
}

/** A steel button: gold edge and text when focused (D-pad) or pressed. */
@Composable
fun MetalButton(text: String, modifier: Modifier = Modifier, primary: Boolean = false, enabled: Boolean = true,
                onClick: () -> Unit) {
    val source = remember { MutableInteractionSource() }
    val focused by source.collectIsFocusedAsState()
    val pressed by source.collectIsPressedAsState()
    val hot = (focused || pressed) && enabled
    Box(
        modifier
            .clickable(source, indication = null, enabled = enabled, onClick = onClick)
            .background(Brush.verticalGradient(
                if (pressed) listOf(Color(0xFF15171A), Color(0xFF22252A))
                else if (primary) listOf(Color(0xFF3A3326), Color(0xFF1E1A13))
                else listOf(Color(0xFF353940), Color(0xFF1C1F23))))
            .drawBehind {
                if (pressed) bevel(UT.EdgeDark, UT.EdgeLight, 2.dp.toPx()) else bevel(UT.EdgeLight, UT.EdgeDark, 2.dp.toPx())
            }
            .then(if (hot) Modifier.border(2.dp, UT.Gold) else Modifier)
            .padding(horizontal = 22.dp, vertical = 9.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text.uppercase(), fontSize = 13.sp, fontWeight = FontWeight.Bold, letterSpacing = 2.sp,
             color = when {
                 !enabled -> UT.TextDim.copy(alpha = 0.5f)
                 hot -> UT.GoldLight
                 primary -> UT.Gold
                 else -> UT.Text
             })
    }
}

/** A selectable steel plate (e.g. an install location). */
@Composable
fun SelectPlate(title: String, detail: String, selected: Boolean, modifier: Modifier = Modifier,
                enabled: Boolean = true, onClick: () -> Unit) {
    val source = remember { MutableInteractionSource() }
    val focused by source.collectIsFocusedAsState()
    Row(
        modifier
            .clickable(source, indication = null, enabled = enabled, onClick = onClick)
            .background(if (selected) Color(0xFF2B2720) else Color(0xFF1A1C20))
            .drawBehind { bevel(UT.EdgeDark, UT.EdgeLight.copy(alpha = 0.6f), 1.5.dp.toPx()) }
            .then(if (focused && enabled) Modifier.border(2.dp, UT.Gold) else Modifier)
            .padding(horizontal = 12.dp, vertical = 9.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(16.dp).border(2.dp, if (selected) UT.Gold else UT.TextDim), contentAlignment = Alignment.Center) {
            if (selected) Box(Modifier.size(8.dp).background(UT.Gold))
        }
        Column(Modifier.padding(start = 14.dp)) {
            Text(title, color = if (enabled) UT.Text else UT.TextDim, fontSize = 14.sp, fontWeight = FontWeight.SemiBold,
                 maxLines = 1)
            Text(detail, color = UT.TextDim, fontSize = 12.sp, maxLines = 1)
        }
    }
}

/** A recessed trough with a gold fill. */
@Composable
fun UtProgressBar(fraction: Float, modifier: Modifier = Modifier, height: Dp = 14.dp) {
    Box(
        modifier
            .height(height)
            .background(Color(0xFF0A0B0D))
            .drawBehind {
                bevel(UT.EdgeDark, UT.EdgeLight.copy(alpha = 0.5f), 1.5.dp.toPx())
                val w = (size.width - 4.dp.toPx()) * fraction.coerceIn(0f, 1f)
                if (w > 0) {
                    drawRect(Brush.verticalGradient(listOf(UT.GoldLight, UT.Gold, UT.GoldDark)),
                             Offset(2.dp.toPx(), 2.dp.toPx()), Size(w, size.height - 4.dp.toPx()))
                }
            },
    )
}

@Composable
fun StepRow(title: String, state: Int /* 0 waiting, 1 active, 2 done, 3 failed */) {
    Row(Modifier.padding(vertical = 3.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(12.dp).background(when (state) {
            1 -> UT.Gold
            2 -> Color(0xFF6E8B3D)
            3 -> UT.Red
            else -> Color(0xFF2C2F34)
        }).border(1.dp, UT.EdgeDark))
        Text(title, modifier = Modifier.padding(start = 12.dp), fontSize = 14.sp,
             color = when (state) {
                 1 -> UT.GoldLight
                 2 -> UT.Text
                 3 -> Color(0xFFE57368)
                 else -> UT.TextDim
             },
             fontWeight = if (state == 1) FontWeight.SemiBold else FontWeight.Normal)
    }
}

@Composable
fun SetupScaffold(right: @Composable ColumnScope.() -> Unit) {
    MetalBackground {
        Row(Modifier.fillMaxSize().padding(horizontal = 48.dp, vertical = 24.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(0.9f).fillMaxHeight(), verticalArrangement = androidx.compose.foundation.layout.Arrangement.Center) {
                UtTitle()
                Spacer(Modifier.height(18.dp))
                Text("Game of the Year Edition", color = UT.Text, fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
                Text("with the OldUnreal 469 community patch", color = UT.TextDim, fontSize = 13.sp)
            }
            Spacer(Modifier.width(40.dp))
            Column(Modifier.weight(1.1f).verticalScroll(rememberScrollState()), content = right)
        }
    }
}
