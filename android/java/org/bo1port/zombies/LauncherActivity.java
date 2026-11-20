package org.bo1port.zombies;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Android launcher: renderer choice and user-selected game folder import. */
public class LauncherActivity extends Activity {
    private static final String PREFS = "bo1_launcher";
    private static final String KEY_RENDERER = "renderer";
    private static final String KEY_TREE = "game_tree_uri";
    private static final int REQUEST_GAME_FOLDER = 4102;
    private static final String DXVK = "dxvk";
    private static final String NATIVE = "native-vulkan";

    private String selected = DXVK;
    private TextView dxvkCard, nativeCard, status, folderStatus;
    private Button startButton, folderButton;
    private final ExecutorService importer = Executors.newSingleThreadExecutor();

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

    private File gameDirectory() {
        File files = getExternalFilesDir(null);
        return new File(files != null ? files : getFilesDir(), "game");
    }

    private boolean hasGameFiles() {
        File game = gameDirectory();
        return new File(game, "localization.txt").isFile() && new File(game, "zone").isDirectory();
    }

    private void updateFolderStatus() {
        if (folderStatus == null) return;
        if (hasGameFiles()) {
            folderStatus.setText("Arquivos do jogo prontos. Pasta interna: " + gameDirectory().getAbsolutePath());
            folderStatus.setTextColor(Color.rgb(154, 220, 184));
            if (startButton != null) {
                startButton.setEnabled(true);
                startButton.setAlpha(1.0f);
            }
        } else {
            folderStatus.setText("Nenhuma instalação válida selecionada. Escolha a pasta que contém localization.txt e zone/.");
            folderStatus.setTextColor(Color.rgb(255, 202, 112));
            if (startButton != null) {
                startButton.setEnabled(false);
                startButton.setAlpha(0.55f);
            }
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

    private void chooseGameFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_GAME_FOLDER);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_GAME_FOLDER || resultCode != RESULT_OK || data == null || data.getData() == null)
            return;

        Uri tree = data.getData();
        final int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        try {
            getContentResolver().takePersistableUriPermission(tree, flags);
        } catch (Exception ignored) {
            // The current grant can still be used for this import.
        }

        folderButton.setEnabled(false);
        startButton.setEnabled(false);
        folderStatus.setText("Lendo a pasta selecionada...");
        folderStatus.setTextColor(Color.WHITE);

        importer.execute(() -> {
            try {
                Uri root = DocumentsContract.buildDocumentUriUsingTree(
                    tree, DocumentsContract.getTreeDocumentId(tree));
                if (!containsGameMarker(root))
                    throw new Exception("A pasta escolhida não contém localization.txt e a pasta zone/. Selecione a pasta principal da instalação do Black Ops.");

                File destination = gameDirectory();
                if (!destination.exists() && !destination.mkdirs())
                    throw new Exception("Não foi possível criar a pasta interna do jogo.");

                copyDirectory(tree, root, destination);
                runOnUiThread(() -> {
                    getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_TREE, tree.toString()).apply();
                    folderStatus.setText("Importação concluída. Os arquivos foram copiados para a pasta do aplicativo.");
                    folderStatus.setTextColor(Color.rgb(154, 220, 184));
                    folderButton.setEnabled(true);
                    updateFolderStatus();
                });
            } catch (Exception e) {
                final String message = e.getMessage() != null ? e.getMessage() : e.toString();
                runOnUiThread(() -> {
                    folderButton.setEnabled(true);
                    folderStatus.setText("Falha ao importar: " + message);
                    folderStatus.setTextColor(Color.rgb(255, 150, 130));
                    updateFolderStatus();
                    new AlertDialog.Builder(this)
                        .setTitle("Não foi possível importar a pasta")
                        .setMessage(message)
                        .setPositiveButton("OK", null)
                        .show();
                });
            }
        });
    }

    private boolean containsGameMarker(Uri document) {
        Cursor cursor = null;
        boolean localization = false, zone = false;
        try {
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                document, DocumentsContract.getDocumentId(document));
            cursor = getContentResolver().query(children,
                new String[] { DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE }, null, null, null);
            if (cursor == null) return false;
            while (cursor.moveToNext()) {
                String name = cursor.getString(0);
                String mime = cursor.getString(1);
                if ("localization.txt".equalsIgnoreCase(name)) localization = true;
                if ("zone".equalsIgnoreCase(name) &&
                    DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) zone = true;
            }
        } catch (Exception ignored) {
            return false;
        } finally {
            if (cursor != null) cursor.close();
        }
        return localization && zone;
    }

    private void copyDirectory(Uri tree, Uri directory, File destination) throws Exception {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
            tree, DocumentsContract.getDocumentId(directory));
        Cursor cursor = getContentResolver().query(children,
            new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE }, null, null, null);
        if (cursor == null) throw new Exception("O Android não conseguiu listar o conteúdo da pasta.");
        try {
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                if (name == null || name.length() == 0 || name.equals(".") || name.equals(".."))
                    continue;
                Uri child = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                File output = new File(destination, name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (!output.exists() && !output.mkdirs())
                        throw new Exception("Não foi possível criar a pasta " + name);
                    copyDirectory(tree, child, output);
                } else {
                    InputStream input = getContentResolver().openInputStream(child);
                    if (input == null) throw new Exception("Não foi possível ler o arquivo " + name);
                    OutputStream out = new FileOutputStream(output);
                    try {
                        byte[] buffer = new byte[1024 * 1024];
                        int count;
                        while ((count = input.read(buffer)) != -1) out.write(buffer, 0, count);
                        out.flush();
                    } finally {
                        try { input.close(); } catch (Exception ignored) {}
                        try { out.close(); } catch (Exception ignored) {}
                    }
                }
            }
        } finally {
            cursor.close();
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
            if (NATIVE.equals(selected)) {
                new AlertDialog.Builder(this)
                    .setTitle("Vulkan Nativo v0.1 — experimental")
                    .setMessage("A base Vulkan ainda não está ligada ao renderizador D3D9 do Black Ops. Selecione DXVK Native 1.9.2 para jogar.")
                    .setPositiveButton("Entendi", null)
                    .show();
                return;
            }
            Intent game = new Intent(this, Bo1Activity.class);
            game.putExtra("bo1.renderer", DXVK);
            startActivity(game);
        });
        add(root, startButton, -1, 48, 12);
        updateFolderStatus();

        TextView footer = text("A pasta escolhida é importada para o armazenamento do aplicativo.", 10, false, Color.rgb(125, 137, 154));
        add(root, footer, -1, 28, 6);
        setContentView(scroll);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        importer.shutdown();
    }
}
