// Reports generated modules that the browser rejects, so code generation bugs can be reproduced.
// Runs in the main thread and in every worker, wrapping the constructor CodeGen uses for each block.
(function () {
	var Original = WebAssembly.Module;
	if (Original.__playReportsErrors) return;
	function ReportingModule(bytes, options) {
		try {
			return options === undefined ? new Original(bytes) : new Original(bytes, options);
		} catch (e) {
			if (e instanceof WebAssembly.CompileError) {
				// Copy out of the shared heap, then base64-encode.
				var copy = new Uint8Array(bytes.slice ? bytes.slice() : bytes);
				var binary = "";
				for (var i = 0; i < copy.length; i++) binary += String.fromCharCode(copy[i]);
				(typeof err === "function" ? err : console.error)("PLAYJS_BAD_WASM " + e.message + " | " + btoa(binary));
			}
			throw e;
		}
	}
	ReportingModule.prototype = Original.prototype;
	Object.setPrototypeOf(ReportingModule, Original);
	ReportingModule.__playReportsErrors = true;
	WebAssembly.Module = ReportingModule;
})();
