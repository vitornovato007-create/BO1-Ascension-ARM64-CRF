package org.bo1port.zombies;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;


/** Android launcher: renderer choice and direct access to the original game folder. */
public class LauncherActivity extends Activity {
    private static final String PREFS = "bo1_launcher";
    private static final String KEY_RENDERER = "renderer";
    private static final String KEY_GAME_PATH = "game_path";
    private static final int REQUEST_GAME_FOLDER = 4102;
    private static final String DXVK = "dxvk";
    private static final String NATIVE = "native-vulkan";

    private String selected = DXVK;
    private TextView dxvkCard, nativeCard, status, folderStatus;
    private Button startButton, folderButton, accessButton;

    private int dp(float value) {
        return (int)(value * getResources().getDisplayMetrics().density + 0.5f);
    }

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
        t.setIncludeFontPadding(false);
        return t;
    }

    private void add(LinearLayout parent, View child, int width, int height, int top) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(
            width < 0 ? width : dp(width), height < 0 ? height : dp(height));
        p.topMargin = dp(top);
        parent.addView(child, p);
    }

    private TextView rendererCard(String title, String subtitle, boolean experimental) {
        TextView card = text(title + "\n" + subtitle, 14, true, Color.WHITE);
        card.setPadding(dp(14), dp(9), dp(14), dp(9));
        card.setGravity(Gravity.CENTER_VERTICAL);
        card.setMaxLines(2);
        card.setBackground(shape(Color.rgb(30, 36, 48), Color.rgb(66, 76, 92)));
        card.setOnClickListener(v -> {
            selected = experimental ? NATIVE : DXVK;
            updateSelection();
        });
        return card;
    }

    private boolean hasAllFilesAccess() {
        return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager();
    }

    private String selectedGamePath() {
        return getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_GAME_PATH, "");
    }

    private boolean hasGameFiles() {
        String path = selectedGamePath();
        if (path == null || path.isEmpty() || !hasAllFilesAccess()) return false;
        File game = new File(path);
        return new File(game, "localization.txt").isFile() && new File(game, "zone").isDirectory();
    }

    private void updateFolderStatus() {
        if (folderStatus == null) return;
        if (!hasAllFilesAccess()) {
            folderStatus.setText("Permita o acesso a todos os arquivos para o motor ler diretamente a pasta original do jogo.");
            folderStatus.setTextColor(Color.rgb(255, 202, 112));
        } else if (hasGameFiles()) {
            folderStatus.setText("Pasta do jogo: " + selectedGamePath() + "\nLeitura direta ativada — nenhum arquivo será copiado.");
            folderStatus.setTextColor(Color.rgb(154, 220, 184));
        } else {
            folderStatus.setText("Nenhuma pasta válida selecionada. Escolha a pasta que contém localization.txt e zone/.");
            folderStatus.setTextColor(Color.rgb(255, 202, 112));
        }
        if (startButton != null) {
            startButton.setEnabled(hasGameFiles());
            startButton.setAlpha(hasGameFiles() ? 1.0f : 0.55f);
        }
        if (accessButton != null) {
            accessButton.setText(hasAllFilesAccess() ? "ACESSO AOS ARQUIVOS PERMITIDO" : "PERMITIR ACESSO A TODOS OS ARQUIVOS");
            accessButton.setEnabled(!hasAllFilesAccess());
            accessButton.setAlpha(hasAllFilesAccess() ? 0.65f : 1.0f);
        }
    }

    private void updateSelection() {
        boolean nativeSelected = NATIVE.equals(selected);
        dxvkCard.setBackground(shape(Color.rgb(30, 36, 48),
            nativeSelected ? Color.rgb(66, 76, 92) : Color.rgb(54, 190, 126)));
        nativeCard.setBackground(shape(Color.rgb(30, 36, 48),
            nativeSelected ? Color.rgb(230, 166, 58) : Color.rgb(66, 76, 92)));
        status.setText(nativeSelected
            ? "Vulkan Nativo v0.1: base Vulkan/WSI experimental; a renderização completa do BO1 ainda não está conectada."
            : "DXVK Native 1.9.2: renderizador atual e recomendado para iniciar o jogo.");
        status.setTextColor(nativeSelected ? Color.rgb(255, 202, 112) : Color.rgb(154, 220, 184));
    }

    private void requestAllFilesAccess() {
        if (Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager()) {
            updateFolderStatus();
            return;
        }
        new AlertDialog.Builder(this)
            .setTitle("Permitir acesso aos arquivos")
            .setMessage("O Black Ops precisa ler diretamente os arquivos da pasta que você escolher. Ative \"Permitir gerenciamento de todos os arquivos\" na próxima tela. O aplicativo não copiará nem moverá os arquivos do jogo.")
            .setNegativeButton("Cancelar", null)
            .setPositiveButton("Abrir configurações", (dialog, which) -> {
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
                } catch (Exception e) {
                    startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                }
            }).show();
    }

    private void chooseGameFolder() {
        if (!hasAllFilesAccess()) {
            requestAllFilesAccess();
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_GAME_FOLDER);
    }

    private String filesystemPathFromTree(Uri tree) throws Exception {
        String documentId = DocumentsContract.getTreeDocumentId(tree);
        int separator = documentId.indexOf(':');
        if (separator <= 0) throw new Exception("O Android não informou um volume de armazenamento reconhecido.");
        String volume = documentId.substring(0, separator);
        String relative = documentId.substring(separator + 1);
        File volumeRoot = "primary".equalsIgnoreCase(volume)
            ? Environment.getExternalStorageDirectory() : new File("/storage", volume);
        File selectedFolder = relative.isEmpty() ? volumeRoot : new File(volumeRoot, relative);
        String canonical = selectedFolder.getCanonicalPath();
        if (!selectedFolder.isDirectory()) throw new Exception("Não foi possível acessar o caminho físico da pasta selecionada.");
        return canonical;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_GAME_FOLDER || resultCode != RESULT_OK || data == null || data.getData() == null)
            return;
        Uri tree = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(tree,
                data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION));
            String path = filesystemPathFromTree(tree);
            File game = new File(path);
            if (!new File(game, "localization.txt").isFile() || !new File(game, "zone").isDirectory())
                throw new Exception("A pasta escolhida não contém localization.txt e zone/. Selecione a pasta principal da instalação do Black Ops.");
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_GAME_PATH, path).apply();
            folderStatus.setText("Pasta selecionada. O jogo lerá os arquivos diretamente, sem copiá-los.");
            updateFolderStatus();
        } catch (Exception e) {
            String message = e.getMessage() != null ? e.getMessage() : e.toString();
            new AlertDialog.Builder(this).setTitle("Não foi possível usar essa pasta")
                .setMessage(message + "\n\nEscolha uma pasta no armazenamento interno compartilhado ou em um volume externo acessível.")
                .setPositiveButton("OK", null).show();
        }
    }

    @Override
    protected void onCreate(Bundle state) {
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
        root.setPadding(dp(24), dp(12), dp(24), dp(16));
        root.setBackgroundColor(Color.rgb(12, 16, 24));
        scroll.addView(root);

        TextView eyebrow = text("CALL OF DUTY", 12, true, Color.rgb(180, 190, 205));
        add(root, eyebrow, -1, 22, 0);
        TextView title = text("BLACK OPS", 27, true, Color.WHITE);
        add(root, title, -1, 38, 0);
        TextView edition = text("ANDROID PORT  •  ZOMBIES", 11, true, Color.rgb(145, 160, 180));
        add(root, edition, -1, 22, 0);

        TextView folderHeading = text("ARQUIVOS DO JOGO", 13, true, Color.WHITE);
        folderHeading.setGravity(Gravity.CENTER_VERTICAL);
        add(root, folderHeading, -1, 28, 12);

        accessButton = new Button(this);
        accessButton.setTextSize(12);
        accessButton.setAllCaps(false);
        add(root, accessButton, -1, 42, 4);
        accessButton.setOnClickListener(v -> requestAllFilesAccess());

        folderButton = new Button(this);
        folderButton.setText("SELECIONAR PASTA DO JOGO");
        folderButton.setTextSize(13);
        folderButton.setAllCaps(false);
        add(root, folderButton, -1, 46, 4);
        folderButton.setOnClickListener(v -> chooseGameFolder());

        folderStatus = text("", 11, false, Color.WHITE);
        folderStatus.setGravity(Gravity.CENTER);
        folderStatus.setPadding(dp(8), dp(5), dp(8), dp(5));
        add(root, folderStatus, -1, -2, 4);
        updateFolderStatus();

        TextView section = text("SELECIONE O RENDERIZADOR", 13, true, Color.WHITE);
        section.setGravity(Gravity.CENTER_VERTICAL);
        add(root, section, -1, 30, 10);

        dxvkCard = rendererCard("DXVK Native 1.9.2", "Renderizador atual • recomendado", false);
        add(root, dxvkCard, -1, 68, 4);
        nativeCard = rendererCard("Vulkan Nativo", "Backend próprio em desenvolvimento • experimental v0.1", true);
        add(root, nativeCard, -1, 68, 8);

        status = text("", 11, false, Color.WHITE);
        status.setGravity(Gravity.CENTER);
        status.setPadding(dp(10), dp(7), dp(10), dp(7));
        status.setBackground(shape(Color.rgb(24, 29, 39), 0));
        add(root, status, -1, -2, 10);
        updateSelection();

        startButton = new Button(this);
        startButton.setText("INICIAR JOGO");
        startButton.setTextSize(14);
        startButton.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        startButton.setTextColor(Color.WHITE);
        startButton.setAllCaps(false);
        startButton.setBackground(shape(Color.rgb(36, 126, 91), 0));
        startButton.setOnClickListener(v -> {
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_RENDERER, selected).apply();
            Intent game = new Intent(this, Bo1Activity.class);
            game.putExtra("bo1.renderer", selected);
            game.putExtra("bo1.game_path", selectedGamePath());
            startActivity(game);
        });
        add(root, startButton, -1, 48, 12);
        updateFolderStatus();

        TextView footer = text("Os arquivos são lidos diretamente da pasta original. Nenhuma cópia é criada.", 10, false, Color.rgb(125, 137, 154));
        add(root, footer, -1, 28, 6);
        setContentView(scroll);
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateFolderStatus();
    }
}
