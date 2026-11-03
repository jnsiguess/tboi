package ru.isaac.port;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsProvider;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileNotFoundException;

/*
 * THE PORT'S FILES IN THE SYSTEM'S OWN FILE MANAGER (the owner's item 14,
 * 23.09.2026): "a button opens the system file manager - the system's, not a
 * vendor's".
 *
 * The config file lives in the port's folder, Android/media/<package> since
 * 28.09.2026 (Android/data/<package>/files before), and since Android 11 the
 * system file manager (DocumentsUI) will not open Android/data at all. So
 * the port offers its folder itself, as a documents root of its own - the way
 * Android wants an app's files shown: the system's Files app lists "TBOI: Rebirth"
 * among its places, and opens, edits and saves the files through this class.
 * Nothing outside the app's own folder is reachable through it.
 *
 * The folder's own document id is "root", not the empty string it used to be:
 * the Files app builds .../document/<id> from it, an empty id makes a path with
 * nothing at its end, and the folder opened on "couldn't load content" (the
 * owner's eighth round, 24.09.2026). The place is called by the game's name.
 */
public class PortFilesProvider extends DocumentsProvider {
    static final String AUTHORITY = "com.tboi.rebirth.files";
    static final String ROOT = "port";
    static final String ROOT_DOC = "root";
    static final String TITLE = "TBOI: Rebirth";

    private static final String[] ROOT_COLUMNS = {
        Root.COLUMN_ROOT_ID, Root.COLUMN_FLAGS, Root.COLUMN_TITLE, Root.COLUMN_DOCUMENT_ID,
        Root.COLUMN_ICON,
    };
    private static final String[] DOC_COLUMNS = {
        Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
        Document.COLUMN_FLAGS, Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED,
    };

    /*
     * The port's folder, the one answer the game uses too (PortDirs): no quiet
     * fallback into Android/data any more - that made a second config nobody
     * read. With no folder at all the root is empty rather than elsewhere.
     */
    private File base() {
        File dir = getContext() != null ? PortDirs.port(getContext()) : null;
        return dir != null ? dir : new File("/proc/self/nonexistent");
    }

    /*
     * What the file manager does not see: the hidden files (.nomedia, the
     * backup marks, .moving, .port-aimlock-v1) and the writer's own half-way
     * files (name.tmp, name.ready) - touching those would break the recovery.
     */
    private static boolean hidden(File f) {
        String n = f.getName();
        return n.startsWith(".") || n.endsWith(".tmp") || n.endsWith(".ready");
    }

    /* a document id is "root", or "root/" and its path inside the folder */
    private File fileOf(String id) throws FileNotFoundException {
        File base = base();
        File f;
        if (id == null || id.isEmpty() || id.equals(ROOT_DOC)) f = base;
        else if (id.startsWith(ROOT_DOC + "/")) f = new File(base, id.substring(ROOT_DOC.length() + 1));
        else throw new FileNotFoundException(id);
        try {
            String b = base.getCanonicalPath(), p = f.getCanonicalPath();
            if (!p.equals(b) && !p.startsWith(b + File.separator)) throw new FileNotFoundException(id);
        } catch (java.io.IOException e) {
            throw new FileNotFoundException(id);
        }
        return f;
    }

    private String idOf(File f) {
        String b = base().getAbsolutePath(), p = f.getAbsolutePath();
        return p.equals(b) ? ROOT_DOC : ROOT_DOC + "/" + p.substring(b.length() + 1);
    }

    private static String mimeOf(File f) {
        if (f.isDirectory()) return Document.MIME_TYPE_DIR;
        String name = f.getName(), ext = name.contains(".") ? name.substring(name.lastIndexOf('.') + 1) : "";
        if (ext.equalsIgnoreCase("ini") || ext.equalsIgnoreCase("txt")) return "text/plain";
        String m = MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext.toLowerCase());
        return m != null ? m : "application/octet-stream";
    }

    private void row(MatrixCursor c, File f) {
        int flags = f.isDirectory() ? Document.FLAG_DIR_SUPPORTS_CREATE
                                    : Document.FLAG_SUPPORTS_WRITE | Document.FLAG_SUPPORTS_DELETE;
        c.newRow()
            .add(Document.COLUMN_DOCUMENT_ID, idOf(f))
            .add(Document.COLUMN_DISPLAY_NAME, idOf(f).equals(ROOT_DOC) ? TITLE : f.getName())
            .add(Document.COLUMN_MIME_TYPE, mimeOf(f))
            .add(Document.COLUMN_FLAGS, flags)
            .add(Document.COLUMN_SIZE, f.isDirectory() ? null : f.length())
            .add(Document.COLUMN_LAST_MODIFIED, f.lastModified());
    }

    @Override
    public boolean onCreate() { return true; }

    @Override
    public Cursor queryRoots(String[] projection) {
        MatrixCursor c = new MatrixCursor(projection != null ? projection : ROOT_COLUMNS);
        c.newRow()
            .add(Root.COLUMN_ROOT_ID, ROOT)
            .add(Root.COLUMN_FLAGS, Root.FLAG_SUPPORTS_CREATE | Root.FLAG_LOCAL_ONLY | Root.FLAG_SUPPORTS_IS_CHILD)
            .add(Root.COLUMN_TITLE, TITLE)
            .add(Root.COLUMN_DOCUMENT_ID, ROOT_DOC)
            .add(Root.COLUMN_ICON, R.mipmap.ic_launcher);
        return c;
    }

    @Override
    public Cursor queryDocument(String documentId, String[] projection) throws FileNotFoundException {
        MatrixCursor c = new MatrixCursor(projection != null ? projection : DOC_COLUMNS);
        row(c, fileOf(documentId));
        return c;
    }

    @Override
    public Cursor queryChildDocuments(String parentId, String[] projection, String sortOrder)
            throws FileNotFoundException {
        MatrixCursor c = new MatrixCursor(projection != null ? projection : DOC_COLUMNS);
        File[] files = fileOf(parentId).listFiles();
        if (files != null) for (File f : files) if (!hidden(f)) row(c, f);
        return c;
    }

    @Override
    public boolean isChildDocument(String parentId, String documentId) {
        return documentId.startsWith(parentId + "/");
    }

    @Override
    public ParcelFileDescriptor openDocument(String documentId, String mode, CancellationSignal signal)
            throws FileNotFoundException {
        /*
         * "w" alone does NOT empty the file from Android 10 on (the skeptic read
         * it in the platform's source): an editor that saves a shorter text
         * left the old one's tail behind it, and the last line of a key wins in
         * our parser. So a plain "w" is "wt". "rw" stays as it is - an editor
         * that reads and writes through one descriptor would read nothing.
         */
        String m = "w".equals(mode) ? "wt" : mode;
        return ParcelFileDescriptor.open(fileOf(documentId), ParcelFileDescriptor.parseMode(m));
    }

    @Override
    public String createDocument(String parentId, String mimeType, String displayName)
            throws FileNotFoundException {
        File f = new File(fileOf(parentId), displayName);
        try {
            if (Document.MIME_TYPE_DIR.equals(mimeType) ? !f.mkdir() : !f.createNewFile())
                throw new FileNotFoundException(displayName);
        } catch (java.io.IOException e) {
            throw new FileNotFoundException(displayName);
        }
        return idOf(f);
    }

    @Override
    public void deleteDocument(String documentId) throws FileNotFoundException {
        File f = fileOf(documentId);
        if (idOf(f).equals(ROOT_DOC) || !f.delete()) throw new FileNotFoundException(documentId);
    }
}
