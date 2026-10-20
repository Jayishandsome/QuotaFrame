// butler-stt: turns one WAV file into text with Apple's own speech recognition.
//
//   butler-stt --file speech.wav [--locale zh-HK] [--engine auto|analyzer|legacy]
//   butler-stt --prepare [--locale zh-TW]      download the language model / ask permission
//   butler-stt --list-locales
//
// Prints one JSON line: {"text": ..., "engine": ...} or {"error": code, "detail": ...}.
// macOS 26+: SpeechAnalyzer/SpeechTranscriber, which runs entirely on this Mac.
// Older systems or failures: SFSpeechRecognizer (on-device when the language
// supports it; otherwise Apple may process the audio on its servers).
import AVFoundation
import Foundation
import Speech

struct Failure: Error {
    let code: String
    var detail: String = ""
}

func emit(_ object: [String: Any]) {
    if let data = try? JSONSerialization.data(withJSONObject: object, options: []) {
        FileHandle.standardOutput.write(data)
        FileHandle.standardOutput.write(Data("\n".utf8))
    }
}

func argument(_ name: String) -> String? {
    let args = CommandLine.arguments
    guard let index = args.firstIndex(of: name), index + 1 < args.count else { return nil }
    return args[index + 1]
}

#if !LEGACY_ONLY
@available(macOS 26.0, *)
enum Analyzer {
    static func resolve(_ identifier: String) async -> Locale? {
        for candidate in [identifier, "zh-Hant-TW", "zh-TW"] {
            if let locale = await SpeechTranscriber.supportedLocale(equivalentTo: Locale(identifier: candidate)) {
                return locale
            }
        }
        return nil
    }

    static func prepare(_ identifier: String) async throws -> SpeechTranscriber {
        guard let locale = await resolve(identifier) else {
            throw Failure(code: "locale_unsupported", detail: identifier)
        }
        let transcriber = SpeechTranscriber(locale: locale, transcriptionOptions: [],
                                            reportingOptions: [], attributeOptions: [])
        if let request = try await AssetInventory.assetInstallationRequest(supporting: [transcriber]) {
            try await request.downloadAndInstall()
        }
        return transcriber
    }

    static func collect(_ transcriber: SpeechTranscriber) async throws -> String {
        var text = ""
        for try await result in transcriber.results {
            text += String(result.text.characters)
        }
        return text
    }

    static func transcribe(_ url: URL, _ identifier: String) async throws -> String {
        let transcriber = try await prepare(identifier)
        let analyzer = SpeechAnalyzer(modules: [transcriber])
        let file = try AVAudioFile(forReading: url)
        async let collected = collect(transcriber)
        if let last = try await analyzer.analyzeSequence(from: file) {
            try await analyzer.finalizeAndFinish(through: last)
        } else {
            await analyzer.cancelAndFinishNow()
        }
        return try await collected
    }

    static func locales() async -> [String] {
        let all = await SpeechTranscriber.supportedLocales
        return all.map { $0.identifier }.sorted()
    }
}
#endif

enum Legacy {
    static func authorize() async -> SFSpeechRecognizerAuthorizationStatus {
        await withCheckedContinuation { continuation in
            SFSpeechRecognizer.requestAuthorization { continuation.resume(returning: $0) }
        }
    }

    static func transcribe(_ url: URL, _ identifier: String) async throws -> String {
        guard await authorize() == .authorized else { throw Failure(code: "speech_permission_denied") }
        guard let recognizer = SFSpeechRecognizer(locale: Locale(identifier: identifier)),
              recognizer.isAvailable else {
            throw Failure(code: "locale_unsupported", detail: identifier)
        }
        recognizer.queue = OperationQueue()
        let request = SFSpeechURLRecognitionRequest(url: url)
        request.shouldReportPartialResults = false
        if recognizer.supportsOnDeviceRecognition { request.requiresOnDeviceRecognition = true }
        return try await withCheckedThrowingContinuation { continuation in
            let lock = NSLock()
            var finished = false
            recognizer.recognitionTask(with: request) { result, error in
                lock.lock()
                defer { lock.unlock() }
                if finished { return }
                if let result, result.isFinal {
                    finished = true
                    continuation.resume(returning: result.bestTranscription.formattedString)
                } else if let error {
                    finished = true
                    let ns = error as NSError
                    if ns.code == 1110 {  // no speech detected
                        continuation.resume(returning: "")
                    } else {
                        continuation.resume(throwing: Failure(code: "recognition_failed", detail: ns.localizedDescription))
                    }
                }
            }
        }
    }
}

@main
struct ButlerSTT {
    static func main() async {
        let identifier = argument("--locale") ?? "zh-TW"
        let engine = argument("--engine") ?? "auto"
        do {
            if CommandLine.arguments.contains("--list-locales") {
                #if !LEGACY_ONLY
                if #available(macOS 26.0, *) {
                    emit(["locales": await Analyzer.locales(), "engine": "SpeechAnalyzer"])
                    return
                }
                #endif
                emit(["locales": SFSpeechRecognizer.supportedLocales().map { $0.identifier }.sorted(),
                      "engine": "SFSpeechRecognizer"])
                return
            }
            if CommandLine.arguments.contains("--prepare") {
                #if !LEGACY_ONLY
                if #available(macOS 26.0, *), engine != "legacy" {
                    _ = try await Analyzer.prepare(identifier)
                    emit(["ok": true, "engine": "SpeechAnalyzer"])
                    return
                }
                #endif
                guard await Legacy.authorize() == .authorized else {
                    throw Failure(code: "speech_permission_denied")
                }
                emit(["ok": true, "engine": "SFSpeechRecognizer"])
                return
            }
            guard let path = argument("--file") else { throw Failure(code: "usage") }
            let url = URL(fileURLWithPath: path)
            var text: String? = nil
            var used = "SFSpeechRecognizer"
            var firstError: Failure? = nil
            #if !LEGACY_ONLY
            if #available(macOS 26.0, *), engine != "legacy" {
                do {
                    text = try await Analyzer.transcribe(url, identifier)
                    used = "SpeechAnalyzer"
                } catch let failure as Failure {
                    firstError = failure
                } catch {
                    firstError = Failure(code: "analyzer_failed", detail: "\(error)")
                }
            }
            #endif
            if text == nil && engine != "analyzer" {
                text = try await Legacy.transcribe(url, identifier)
            }
            guard let text else { throw firstError ?? Failure(code: "recognition_failed") }
            emit(["text": text.trimmingCharacters(in: .whitespacesAndNewlines), "engine": used])
        } catch let failure as Failure {
            emit(["error": failure.code, "detail": failure.detail])
            exit(2)
        } catch {
            emit(["error": "recognition_failed", "detail": "\(error)"])
            exit(2)
        }
    }
}
