#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QLabel>
#include <QClipboard>
#include <QMessageBox>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow)
{
	ui->setupUi(this);
	setWindowFlags(windowFlags() & ~(Qt::WindowMaximizeButtonHint));
	setFixedSize(this->width(), this->height());
	
	sock = new QTcpSocket(this);
	sock->abort();
	connect(sock, SIGNAL(readyRead()), this, SLOT(on_sock_readable()));
	connect(sock, SIGNAL(error(QAbstractSocket::SocketError)), \
		this, SLOT(ReadError(QAbstractSocket::SocketError)));
	status = "Disconnected.";
	status_lbl = new QLabel();
	status_lbl->setText(status);
	ui->statusBar->addWidget(status_lbl);
	ui->pushButtonConnect->setEnabled(true);
	ui->pushButtonDisconnect->setEnabled(false);
	ui->lineEditPort->setText("8099");
	log_name = "cube_log.txt";
	ui->lineEditLog->setText(log_name);
	ui->pushButtonStartRecord->setEnabled(true);
	ui->pushButtonStopRecord->setEnabled(false);
	ui->pushButtonApplyDebug->setEnabled(false);
	record_en = false;
	connected = false;
	record_file = NULL;
}

MainWindow::~MainWindow()
{
	if (sock) {
		sock->abort();
		delete sock;
	}

	if (record_en) {
		record_en = false;
		if (record_file) {
			record_file->close();
			delete record_file;
			record_file = NULL;
		}
	}

	delete ui;
}

void MainWindow::on_sock_readable()
{
	QByteArray buffer = sock->readAll();
	
	if (!buffer.isEmpty()) {
		ui->textBrowserLogPreview->append(buffer);
		if (record_en) {
			record_file->write(buffer);
		}
	}
}

void MainWindow::on_pushButtonConnect_clicked()
{
	if (ui->lineEditIP->text().isEmpty()) {
		QMessageBox::information(NULL, "Warning", "IP cannot be empty.",
					 QMessageBox::Ok);
		return;
	}
	
	if (ui->lineEditPort->text().isEmpty()) {
		QMessageBox::information(NULL, "Warning", "Port cannot be empty.",
					 QMessageBox::Ok);
		return;
	}
	
	status = "Connecting ...";
	status_lbl->setText(status);
	sock->connectToHost(ui->lineEditIP->text(),
			    ui->lineEditPort->text().toInt());
	if (sock->waitForConnected(1000)) {
		ui->pushButtonDisconnect->setEnabled(true);
		ui->pushButtonConnect->setEnabled(false);
		status = "Connected.";
		status_lbl->setText(status);
		connected = true;
		ui->pushButtonApplyDebug->setEnabled(true);
	} else {
		connected = false;
		ui->pushButtonApplyDebug->setEnabled(false);
		ui->pushButtonDisconnect->setEnabled(false);
		ui->pushButtonConnect->setEnabled(true);
		status = "Connect failed.";
		status_lbl->setText(status);
	}
	
}

void MainWindow::on_pushButtonDisconnect_clicked()
{
	status = "Disconnecting ...";
	status_lbl->setText(status);
	connected = false;
	ui->pushButtonApplyDebug->setEnabled(false);
	sock->abort();
	ui->pushButtonDisconnect->setEnabled(false);
	ui->pushButtonConnect->setEnabled(true);
	status = "Disconnected";
	status_lbl->setText(status);
}

void MainWindow::on_pushButtonClear_clicked()
{
	ui->textBrowserLogPreview->clear();
}

void MainWindow::on_pushButtonCopy_clicked()
{
	QClipboard *board = QApplication::clipboard();
	board->setText(ui->textBrowserLogPreview->document()->toPlainText());
}

void MainWindow::on_pushButtonStartRecord_clicked()
{
	if (ui->lineEditLog->text().isEmpty()) {
		QMessageBox::information(NULL, "Warning", "Log file name cannot be empty.",
					 QMessageBox::Ok);
		return;
	}
	
	log_name = ui->lineEditLog->text();
	if (record_file) {
		record_file->close();
		delete record_file;
		record_file = NULL;
	}
	
	record_file = new QFile(log_name);
	if (!record_file) {
		QMessageBox::information(NULL, "Error", "Cannot new file.",
					 QMessageBox::Ok);
		return;
	}
	record_file->open(QIODevice::Truncate | QIODevice::ReadWrite |
			  QIODevice::Text);
	record_en = true;
	ui->pushButtonStopRecord->setEnabled(true);
	ui->pushButtonStartRecord->setEnabled(false);
}

void MainWindow::on_pushButtonStopRecord_clicked()
{
	ui->pushButtonStopRecord->setEnabled(false);
	ui->pushButtonStartRecord->setEnabled(true);
	record_en = false;
	if (record_file) {
		record_file->close();
		delete record_file;
		record_file = NULL;
	}
}

void MainWindow::on_pushButtonApplyDebug_clicked()
{
	int comp_dbg = ui->spinBoxComp->text().toInt();
	int clia_dbg = ui->spinBoxClia->text().toInt();
	int so_dbg = ui->spinBoxSo->text().toInt();
	int rd_dbg = ui->spinBoxRd->text().toInt();
	int client_dbg = ui->spinBoxClient->text().toInt();
	int touch_dbg = ui->spinBoxTouch->text().toInt();
	int joystick_dbg = ui->spinBoxJoystick->text().toInt();
	QByteArray data;
	
	data.resize(8);
	data[0] = 0x07;
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = 0x00;
	data[4] = 0x00;
	data[5] = 0x00;
	data[6] = 0x00;
	data[7] = 0x00;
	
	sock->write(data);
	
	data.clear();
	data.resize(7);
	data[0] = comp_dbg;
	data[1] = clia_dbg;
	data[2] = so_dbg;
	data[3] = rd_dbg;
	data[4] = client_dbg;
	data[5] = touch_dbg;
	data[6] = joystick_dbg;
	
	sock->write(data);
}
