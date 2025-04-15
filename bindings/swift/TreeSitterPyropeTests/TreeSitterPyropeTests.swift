import XCTest
import SwiftTreeSitter
import TreeSitterPyrope

final class TreeSitterPyropeTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_pyrope())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading Pyrope grammar")
    }
}
