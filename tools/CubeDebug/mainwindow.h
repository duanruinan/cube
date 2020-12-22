#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QString>
#include <QTcpSocket>
#include <QFile>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
	Q_OBJECT
	
public:
	explicit MainWindow(QWidget *parent = 0);
	~MainWindow();
	
private slots:
	void on_pushButtonConnect_clicked();
	
	void on_pushButtonDisconnect_clicked();
	
	void on_sock_readable();
	
	void on_pushButtonClear_clicked();
	
	void on_pushButtonCopy_clicked();
	
	void on_pushButtonStartRecord_clicked();
	
	void on_pushButtonStopRecord_clicked();
	
	void on_pushButtonApplyDebug_clicked();
	
private:
	Ui::MainWindow *ui;
	
	QTcpSocket *sock;
	
	QString status;
	QLabel *status_lbl;
	
	QString log_name;
	
	QFile *record_file;
	
	bool record_en;
	
	bool connected;
};

#endif // MAINWINDOW_H
