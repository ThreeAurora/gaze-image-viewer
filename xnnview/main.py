"""Xnnview —— 实况照片浏览器"""
import sys
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import Qt
from app.main_window import MainWindow


def main():
    app = QApplication(sys.argv)
    app.setApplicationName('Xnnview')
    app.setQuitOnLastWindowClosed(True)

    # XnView MP 风格深色主题
    app.setStyleSheet("""
        QWidget {
            font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
            font-size: 12px;
            color: #CCC;
            background: #000000;
        }
        /* 滚动条 */
        QScrollBar:vertical {
            background: #1e1e1e; width: 10px;
        }
        QScrollBar::handle:vertical {
            background: #555; border-radius: 5px; min-height: 30px;
        }
        QScrollBar::handle:vertical:hover { background: #777; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal {
            background: #1e1e1e; height: 10px;
        }
        QScrollBar::handle:horizontal {
            background: #555; border-radius: 5px; min-width: 30px;
        }
        /* 弹窗 */
        QMessageBox { background: #252525; color: #e0e0e0; }
        QMessageBox QLabel { color: #e0e0e0; }
        QMessageBox QPushButton {
            background: #3a3a3a; color: #e0e0e0; border: 1px solid #555;
            padding: 6px 16px; border-radius: 3px;
        }
        QMessageBox QPushButton:hover { background: #4a4a4a; }
        /* 菜单 */
        QMenu {
            background: #252525; color: #e0e0e0; border: 1px solid #444;
            border-radius: 4px; padding: 4px;
        }
        QMenu::item { padding: 6px 28px; border-radius: 2px; }
        QMenu::item:hover { background: #3a3a3a; }
        QMenu::separator { height: 1px; background: #444; margin: 4px 8px; }
    """)


    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
