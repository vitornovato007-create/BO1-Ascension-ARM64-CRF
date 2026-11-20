package org.bo1port.zombies;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** Native Android launcher. The SDL/game engine is not loaded until the user presses Start Game. */
public class LauncherActivity extends Activity {
    private static final String PREFS = "bo1_launcher";
    private static final String KEY_RENDERER = "renderer";
    private static final String DXVK = "dxvk";
    private static final String NATIVE = "native-vulkan";
    private String selected = DXVK;
    private TextView dxvkCard, nativeCard, status;

    private int dp(float value) { return (int)(value * getResources().getDisplayMetrics().density + 0.5f); }

    private GradientDrawable shape(int color, int stroke) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp(12));
        if (stroke != 0) d.setStroke(dp(2), stroke);
        return d;
    }

    private TextView text(String value, int size, boolean bold, int color) {
        TextView t = new TextView(this);
        t.setText(value);
        t.setTextSize(size);
        t.setTextColor(color);
        t.setGravity(Gravity.CENTER);
        if (bold) t.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return t;
    }

    private void add(LinearLayout parent, View child, int width, int height, int top) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(width < 0 ? width : dp(width), height < 0 ? height : dp(height));
        p.topMargin = dp(top);
        parent.addView(child, p);
        return parent;
    }

    private TextView rendererCard(String title, String subtitle, boolean experimental, boolean chosen) {
        TextView card = text(title + "\n" + subtitle + (experimental ? "\nEXPERIMENTAL — V0.1" : "\nCURRENT WORKING BACKEND"), 16, true, Color.WHITE);
        card.setPadding(dp(16), dp(16), dp(16), dp(16));
        card.setGravity(Gravity.CENTER_VERTICAL);
        card.setBackground(shape(Color.rgb(30, 36, 48), chosen ? Color.rgb(54, 190, 126) : Color.rgb(66, 76, 92)));
        card.setOnClickListener(v -> {
            selected = experimental ? NATIVE : DXVK;
            updateSelection();
        });
        return card;
    }

    private void updateSelection() {
        boolean nativeSelected = NATIVE.equals(selected);
        dxvkCard.setBackground(shape(Color.rgb(30, 36, 48), nativeSelected ? Color.rgb(66, 76, 92) : Color.rgb(54, 190, 126)));
        nativeCard.setBackground(shape(Color.rgb(30, 36, 48), nativeSelected ? Color.rgb(230, 166, 58) : Color.rgb(66, 76, 92)));
        status.setText(nativeSelected
            ? "Vulkan Nativo v0.1 selecionado. Atenção: esta versão inicial testa a base Vulkan/WSI; a renderização completa do BO1 ainda não está conectada. O motor não será iniciado neste modo experimental."
            : "DXVK Native 1.9.2 selecionado. Esta é a opção recomendada para iniciar o jogo nesta versão.");
        status.setTextColor(nativeSelected ? Color.rgb(255, 202, 112) : Color.rgb(154, 220, 184));
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        selected = getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_RENDERER, DXVK);
        if (!DXVK.equals(selected) && !NATIVE.equals(selected)) selected = DXVK;

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setPadding(dp(24), dp(22), dp(24), dp(22));
        root.setBackgroundColor(Color.rgb(12, 16, 24));
        scroll.addView(root);

        TextView eyebrow = text("CALL OF DUTY", 14, true, Color.rgb(180, 190, 205));
        add(root, eyebrow, -1, 28, 0);
        TextView title = text("BLACK OPS", 34, true, Color.WHITE);
        add(root, title, -1, 48, 0);
        TextView edition = text("ANDROID PORT  •  ZOMBIES", 12, true, Color.rgb(145, 160, 180));
        add(root, edition, -1, 28, 0);

        TextView section = text("SELECIONE O RENDERIZADOR", 14, true, Color.WHITE);
        section.setGravity(Gravity.CENTER_VERTICAL);
        add(root, section, -1, 42, 24);

        dxvkCard = rendererCard("DXVK Native 1.9.2", "Renderizador atual • recomendado", false, DXVK.equals(selected));
        add(root, dxvkCard, -1, 92, 8);
        nativeCard = rendererCard("Vulkan Nativo", "Backend próprio em desenvolvimento", true, NATIVE.equals(selected));
        add(root, nativeCard, -1, 92, 12);

        status = text("", 13, false, Color.WHITE);
        status.setGravity(Gravity.CENTER);
        status.setPadding(dp(12), dp(10), dp(12), dp(10));
        status.setBackground(shape(Color.rgb(24, 29, 39), 0));
        add(root, status, -1, -2, 16);
        updateSelection();

        Button start = new Button(this);
        start.setText("INICIAR JOGO");
        start.setTextSize(16);
        start.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        start.setTextColor(Color.WHITE);
        start.setAllCaps(false);
        start.setBackground(shape(Color.rgb(36, 126, 91), 0));
        start.setOnClickListener(v -> {
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_RENDERER, selected).apply();
            if (NATIVE.equals(selected)) {
                new android.app.AlertDialog.Builder(this)
                    .setTitle("Vulkan Nativo v0.1 — experimental")
                    .setMessage("A base Vulkan ainda não está ligada ao renderizador D3D9 do Black Ops. Para evitar que o teste use DXVK sem você perceber, o motor do jogo não será iniciado nesta opção. Selecione DXVK Native 1.9.2 para jogar.")
                    .setPositiveButton("Entendi", (dialog, which) -> {})
                    .show();
                return;
            }
            Intent game = new Intent(this, Bo1Activity.class);
            game.putExtra("bo1.renderer", DXVK);
            startActivity(game);
        });
        add(root, start, -1, 58, 20);

        TextView footer = text("A escolha é aplicada antes de carregar o motor do jogo.", 11, false, Color.rgb(125, 137, 154));
        add(root, footer, -1, 34, 12);
        setContentView(scroll);
    }
}
