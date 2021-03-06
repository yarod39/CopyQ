/*
    Copyright (c) 2013, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "action.h"

Action::Action(const Commands &cmd,
               const QByteArray &input, const QString &outputItemFormat,
               const QString &itemSeparator,
               const QString &outputTabName)
    : QProcess()
    , m_input(input)
    , m_sep(itemSeparator)
    , m_cmds(cmd)
    , m_tab(outputTabName)
    , m_outputFormat(outputItemFormat != "text/plain" ? outputItemFormat : QString())
    , m_errstr()
    , m_lastOutput()
    , m_failed(false)
    , m_firstProcess(NULL)
{
    setProcessChannelMode(QProcess::SeparateChannels);
    connect( this, SIGNAL(error(QProcess::ProcessError)),
             SLOT(actionError(QProcess::ProcessError)) );
    connect( this, SIGNAL(started()),
             SLOT(actionStarted()) );
    connect( this, SIGNAL(finished(int,QProcess::ExitStatus)),
             SLOT(actionFinished()) );
    connect( this, SIGNAL(readyReadStandardError()),
             SLOT(actionErrorOutput()) );

    if ( !outputItemFormat.isEmpty() ) {
        connect( this, SIGNAL(readyReadStandardOutput()),
                 SLOT(actionOutput()) );
    }
}

QString Action::command() const
{
    QString text;
    foreach ( const QStringList &args, m_cmds ) {
        if ( !text.isEmpty() )
            text.append(QChar('|'));
        text.append(args.join(" "));
    }
    return text;
}

void Action::start()
{
    if ( m_cmds.isEmpty() )
        return;

    if ( m_cmds.size() > 1 ) {
        QProcess *lastProcess = new QProcess(this);
        m_firstProcess = lastProcess;
        for ( int i = 0; i + 1 < m_cmds.size(); ++i ) {
            const QStringList &args = m_cmds[i];
            if (args.isEmpty())
                continue;
            QProcess *process = (i + 2 == m_cmds.size()) ? this : new QProcess(this);
            lastProcess->setStandardOutputProcess(process);
            lastProcess->start(args.first(), args.mid(1), QIODevice::ReadWrite);
            lastProcess = process;
        }
    } else {
        m_firstProcess = this;
    }

    const QStringList &args = m_cmds.last();
    QProcess::start(args.isEmpty() ? QString() : args.first(), args.mid(1), QIODevice::ReadWrite);
}

void Action::actionError(QProcess::ProcessError)
{
    if ( state() != Running ) {
        m_failed = true;
        emit actionFinished(this);
    }
}

void Action::actionStarted()
{
    if (m_firstProcess == NULL)
        return;

    // write input
    if ( !m_input.isEmpty() )
        m_firstProcess->write( m_input );
    m_firstProcess->closeWriteChannel();
    m_firstProcess = NULL;

    emit actionStarted(this);
}

void Action::actionFinished()
{
    if ( !m_outputFormat.isEmpty() ) {
        if ( !m_outputData.isNull() ) {
            emit newItem(m_outputData, m_outputFormat, m_tab);
            m_outputData = QByteArray();
        }
    } else if ( !m_lastOutput.isNull() ) {
        actionOutput();
        if ( !m_lastOutput.isNull() ) {
            QStringList items;
            items.append(m_lastOutput);
            emit newItems(items, m_tab);
            m_lastOutput = QString();
        }
    }

    emit actionFinished(this);
}

void Action::actionOutput()
{
    if (!m_outputFormat.isEmpty()) {
        m_outputData.append( readAll() );
        return;
    }

    m_lastOutput.append( QString::fromLocal8Bit(readAll()) );
    if ( m_lastOutput.isEmpty() || m_sep.isEmpty() )
        return;

    // Split to items.
    QStringList items;
    items = m_lastOutput.split(m_sep);
    m_lastOutput = items.takeLast();
    emit newItems(items, m_tab);
}

void Action::actionErrorOutput()
{
    m_errstr += QString::fromLocal8Bit( readAllStandardError() );
}

void Action::terminate()
{
    // try to terminate process
    QProcess::terminate();
    // if process still running: kill it
    if ( !waitForFinished(5000) )
        kill();
}
