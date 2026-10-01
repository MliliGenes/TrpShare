// The browser keeps only one Blob slice in flight. The server writes chunks to disk.
let uploadActive = false;
let uploadCancelled = false;
let uploadController = null;

function rememberUpload(key, id) {
    try { if (id) localStorage.setItem(key, id); else localStorage.removeItem(key); }
    catch (_) { /* Uploading still works if browser storage is unavailable. */ }
}
function previousUpload(key) {
    try { return localStorage.getItem(key); } catch (_) { return null; }
}

async function uploadRequest(url, options = {}) {
    const controller = new AbortController();
    uploadController = controller;
    const timeout = setTimeout(() => controller.abort(), 45000);
    try {
        const response = await fetch(url, {...options, signal: controller.signal});
        const data = await response.json();
        if (!response.ok) {
            const error = new Error(data.error || 'Upload request failed');
            error.status = response.status;
            throw error;
        }
        return data;
    } finally {
        clearTimeout(timeout);
        if (uploadController === controller) uploadController = null;
    }
}

function showUploadProgress(file, offset) {
    const percent = file.size ? Math.floor(offset / file.size * 100) : 100;
    $('#upload-progress').value = percent;
    $('#status').textContent = `${file.name} — ${percent}% (${fmt(offset)} / ${fmt(file.size)})`;
}

async function uploadFile(file, folder) {
    if (!Number.isSafeInteger(file.size)) throw new Error('File size is too large for this browser');
    const key = 'trpshare-upload:' + JSON.stringify([folder, file.name, file.size, file.lastModified]);
    let id = previousUpload(key);
    let session = null;
    try {
        if (id) {
            try { session = await uploadRequest('/api/uploads/' + id); }
            catch (error) {
                if (error.status !== 404) throw error;
                rememberUpload(key, null);

            }
        }
        if (uploadCancelled) throw new Error('Upload cancelled');
        if (!session) {
            if (!id) {
                const bytes = crypto.getRandomValues(new Uint8Array(16));
                id = Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
            }
            rememberUpload(key, id);
            const params = new URLSearchParams({id, name: file.name, size: String(file.size), path: folder});
            session = await uploadRequest('/api/uploads?' + params, {method: 'POST'});
            id = session.id;
            rememberUpload(key, id);
        }
        let offset = session.offset;
        const chunkSize = session.chunkSize;
        showUploadProgress(file, offset);
        let failures = 0;
        while (offset < file.size) {
            if (uploadCancelled) throw new Error('Upload cancelled');
            const end = Math.min(offset + chunkSize, file.size);
            try {
                session = await uploadRequest('/api/uploads/' + id + '?offset=' + offset, {
                    method: 'PUT',
                    headers: {'Content-Type': 'application/octet-stream'},
                    body: file.slice(offset, end)
                });
                offset = session.offset;
                failures = 0;
                showUploadProgress(file, offset);
            } catch (error) {
                if (uploadCancelled) throw error;
                if (++failures > 3) throw error;
                $('#status').textContent = 'Connection interrupted. Retrying chunk…';
                await new Promise(resolve => setTimeout(resolve, failures * 500));
                // A response can be lost after the server commits the chunk.
                // Always consult its offset before retransmitting any bytes.
                try {
                    session = await uploadRequest('/api/uploads/' + id);
                    offset = session.offset;
                    showUploadProgress(file, offset);
                } catch (statusError) {
                    if (uploadCancelled || statusError.status === 404) throw statusError;
                    // Retry at the previous offset; a 409 will trigger another status check.
                }
            }
        }
        if (uploadCancelled) throw new Error('Upload cancelled');
        $('#cancel-upload').disabled = true;
        $('#status').textContent = 'Finalizing ' + file.name + '…';
        await uploadRequest('/api/uploads/' + id + '/complete', {method: 'POST'});
        rememberUpload(key, null);
    } catch (error) {
        if (uploadCancelled && id) {
            try { await uploadRequest('/api/uploads/' + id, {method: 'DELETE'}); }
            catch (_) { /* The user can reselect the file to recover a retained session. */ }
            rememberUpload(key, null);
        }
        throw error;
    }
}

$('#cancel-upload').onclick = () => {
    uploadCancelled = true;
    if (uploadController) uploadController.abort();
};

$('#picker').onchange = async event => {
    const file = event.target.files[0];
    if (!file || uploadActive) return;
    // Snapshot the destination, even if the user navigates during upload.
    const folder = current;
    const status = $('#status');
    uploadActive = true;
    uploadCancelled = false;
    event.target.disabled = true;
    $('#upload-progress').classList.remove('hidden');
    $('#cancel-upload').classList.remove('hidden');
    status.className = 'status';
    try {
        await uploadFile(file, folder);
        status.textContent = 'Uploaded ' + file.name;
        await load();
    } catch (error) {
        status.className = 'status error';
        status.textContent = uploadCancelled ? 'Upload cancelled.' :
            error.message + ' — select the same file in the same folder to resume.';
    } finally {
        uploadActive = false;
        event.target.disabled = false;
        event.target.value = '';
        $('#cancel-upload').classList.add('hidden');
        $('#cancel-upload').disabled = false;
    }
};
